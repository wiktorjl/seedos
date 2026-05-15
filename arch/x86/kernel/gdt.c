// SPDX-License-Identifier: GPL-2.0-only
/*
 * Global Descriptor Table initialization
 *
 * Sets up segment descriptors for kernel and user mode operation.
 * The GDT replaces Limine's default GDT to add user-mode segments
 * required for Ring 3 execution.
 */

#include "gdt.h"
#include "log.h"
#include "vmm.h"

/* Assembly function to reload segment registers */
extern void gdt_reload(void);

/*
 * GDT table - 7 entries total:
 *   0: NULL descriptor (required)
 *   1: Kernel Code (0x08)
 *   2: Kernel Data (0x10)
 *   3: User Data (0x18)   <- Before User Code for SYSRET!
 *   4: User Code (0x20)
 *   5-6: TSS (16 bytes, spans two slots)
 */
static gdt_entry_t gdt[GDT_ENTRIES] __attribute__((aligned(16)));

/* GDTR for lgdt instruction */
static gdtr_t gdtr;

/* Task State Segment */
static x86_tss_t tss __attribute__((aligned(16)));

/*
 * Dedicated IST stacks for high-priority exceptions.
 *
 * NMI, #DF, and #MCE can fire at arbitrary times (including on a
 * corrupt kernel stack); routing them through the TSS Interrupt Stack
 * Table guarantees a known-good stack and avoids triple-faults.
 *
 * Layout per stack (one PAGE_SIZE guard at the bottom):
 *
 *   +------------+ <- top = base + IST_STACK_TOTAL  (TSS points here)
 *   | usable     |    Stack grows down from here.
 *   | 8 KB       |
 *   +------------+ <- base + IST_GUARD_SIZE
 *   | guard page |    Unmapped by gdt_install_ist_guards() after VMM
 *   | 4 KB       |    init. Overflow into it triggers #PF, which is
 *   +------------+ <- base                          delivered on RSP0.
 *
 * KPTI note: these stacks live in kernel-only BSS. If a separate user
 * CR3 is ever added (Meltdown mitigation), the IST stacks must remain
 * mapped in the user PML4 or NMI/#DF/#MCE entry from user mode will
 * itself fault.
 */
#define IST_PAGE_SIZE    4096
#define IST_USABLE_SIZE  8192
#define IST_GUARD_SIZE   IST_PAGE_SIZE
#define IST_STACK_TOTAL  (IST_USABLE_SIZE + IST_GUARD_SIZE)

static uint8_t ist_nmi_stack[IST_STACK_TOTAL]
	__attribute__((aligned(IST_PAGE_SIZE)));
static uint8_t ist_df_stack[IST_STACK_TOTAL]
	__attribute__((aligned(IST_PAGE_SIZE)));
static uint8_t ist_mce_stack[IST_STACK_TOTAL]
	__attribute__((aligned(IST_PAGE_SIZE)));

/**
 * gdt_set_entry - Configure a standard GDT entry
 * @index: GDT slot number
 * @base: Segment base address (ignored for code/data in 64-bit mode)
 * @limit: Segment limit (ignored for code/data in 64-bit mode)
 * @access: Access byte (present, DPL, type, etc.)
 * @flags: Flags nibble (granularity, size, long mode)
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags)
{
    gdt[index].limit_low   = limit & 0xFFFF;
    gdt[index].base_low    = base & 0xFFFF;
    gdt[index].base_mid    = (base >> 16) & 0xFF;
    gdt[index].access      = access;
    gdt[index].limit_flags = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt[index].base_high   = (base >> 24) & 0xFF;
}

/**
 * gdt_set_tss - Configure the TSS descriptor
 *
 * TSS descriptor is 16 bytes in 64-bit mode to hold the full 64-bit base.
 * It occupies slots 5 and 6 in our GDT.
 */
static void gdt_set_tss(void)
{
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(x86_tss_t) - 1;

    /* Cast GDT slot 5 as a TSS descriptor (16 bytes) */
    gdt_tss_entry_t *tss_desc = (gdt_tss_entry_t *)&gdt[5];

    tss_desc->limit_low   = tss_limit & 0xFFFF;
    tss_desc->base_low    = tss_base & 0xFFFF;
    tss_desc->base_mid    = (tss_base >> 16) & 0xFF;
    tss_desc->access      = GDT_ACCESS_TSS;
    tss_desc->limit_flags = (tss_limit >> 16) & 0x0F;  /* No flags for TSS */
    tss_desc->base_mid2   = (tss_base >> 24) & 0xFF;
    tss_desc->base_high   = (tss_base >> 32) & 0xFFFFFFFF;
    tss_desc->reserved    = 0;
}

/**
 * tss_init - Initialize the Task State Segment
 *
 * Clears TSS and sets the I/O permission bitmap offset.
 * RSP0 will be set later when switching to a user process.
 */
static void tss_init(void)
{
    /* Zero the entire TSS */
    uint8_t *p = (uint8_t *)&tss;
    for (size_t i = 0; i < sizeof(x86_tss_t); i++)
        p[i] = 0;

    /*
     * Set IOPB offset beyond the TSS limit to disable I/O permission bitmap.
     * This means all I/O port access from Ring 3 will cause #GP.
     */
    tss.iopb_offset = sizeof(x86_tss_t);

    /*
     * Wire IST entries to the dedicated stacks. The TSS holds the
     * "top" address; CPU pre-decrements RSP on each push, so the
     * first byte written is at (top - 8). We point at the very end
     * of the allocation; gdt_install_ist_guards() later unmaps the
     * bottom IST_GUARD_SIZE bytes to turn overflow into a #PF.
     */
    tss.ist1 = (uint64_t)&ist_nmi_stack[IST_STACK_TOTAL];
    tss.ist2 = (uint64_t)&ist_df_stack[IST_STACK_TOTAL];
    tss.ist3 = (uint64_t)&ist_mce_stack[IST_STACK_TOTAL];
}

/**
 * gdt_install_ist_guards - Unmap the guard page below each IST stack.
 *
 * Called after vmm_init has captured the kernel PML4. An overflowing
 * IST handler will then take a #PF on the guard page (delivered on
 * the regular kernel stack via vector 14's IST=0) instead of silently
 * corrupting adjacent BSS.
 *
 * Soft-fails if the kernel image was mapped with 1 GiB/2 MiB pages -
 * vmm_unmap_page now refuses to split huge pages, and we log a warn
 * rather than panic. Limine's default mapping is 4 KiB pages, so this
 * should succeed on the standard boot path.
 */
void gdt_install_ist_guards(void)
{
    uint64_t kpml4 = vmm_get_kernel_pml4();
    struct { const char *name; uint8_t *base; } stacks[] = {
        { "NMI",  ist_nmi_stack },
        { "#DF",  ist_df_stack  },
        { "#MCE", ist_mce_stack },
    };

    for (size_t i = 0; i < sizeof(stacks) / sizeof(stacks[0]); i++) {
        if (vmm_unmap_page(kpml4, (uint64_t)stacks[i].base) < 0) {
            log_warn("GDT: %s IST guard not installed "
                     "(huge page or unmapped)", stacks[i].name);
        }
    }
    log_info("GDT: IST guard pages installed");
}

/**
 * gdt_init - Initialize and load the GDT
 *
 * Builds the GDT with all required segments, initializes the TSS,
 * loads the GDT with lgdt, reloads segment registers, and loads
 * the TSS with ltr.
 */
void gdt_init(void)
{
    /* Entry 0: NULL descriptor (required by x86) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: Kernel Code (0x08) - 64-bit, DPL=0 */
    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_ACCESS_KERNEL_CODE,
                  GDT_FLAG_GRANULARITY | GDT_FLAGS_CODE64);

    /* Entry 2: Kernel Data (0x10) - DPL=0 */
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_ACCESS_KERNEL_DATA,
                  GDT_FLAG_GRANULARITY | GDT_FLAGS_DATA);

    /* Entry 3: User Data (0x18) - DPL=3 */
    /* NOTE: User Data before User Code for SYSRET compatibility! */
    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_ACCESS_USER_DATA,
                  GDT_FLAG_GRANULARITY | GDT_FLAGS_DATA);

    /* Entry 4: User Code (0x20) - 64-bit, DPL=3 */
    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_ACCESS_USER_CODE,
                  GDT_FLAG_GRANULARITY | GDT_FLAGS_CODE64);

    /* Initialize TSS before creating descriptor */
    tss_init();

    /* Entry 5-6: TSS (16 bytes) */
    gdt_set_tss();

    /* Set up GDTR */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;

    /* Load GDT */
    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    /* Reload segment registers to use new GDT */
    gdt_reload();

    /* Load TSS - selector 0x28 with RPL=0 */
    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS));

    log_debug("GDT: Loaded at 0x%llx, %d entries", (uint64_t)&gdt, GDT_ENTRIES);
    log_debug("TSS: Loaded at 0x%llx, size %d bytes", (uint64_t)&tss, sizeof(x86_tss_t));
}

/**
 * gdt_set_tss_rsp0 - Update kernel stack pointer in TSS
 * @rsp0: New kernel stack pointer
 *
 * Called during context switch. When user code traps to kernel
 * (syscall, interrupt), CPU loads RSP from TSS.rsp0.
 */
void gdt_set_tss_rsp0(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

/**
 * gdt_get_tss - Get pointer to TSS
 */
x86_tss_t *gdt_get_tss(void)
{
    return &tss;
}
