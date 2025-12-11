/*
 * gdt.c - Global Descriptor Table and Task State Segment
 *
 * This file sets up the GDT and TSS required for privilege level separation.
 *
 * GDT Descriptor Format (8 bytes for code/data):
 *
 *   63       56 55   52 51  48 47       40 39       32
 *   ┌─────────┬───────┬──────┬───────────┬───────────┐
 *   │Base High│ Flags │Limit │  Access   │ Base Mid  │
 *   │ (8 bits)│(4 bit)│ High │  (8 bits) │ (8 bits)  │
 *   └─────────┴───────┴──────┴───────────┴───────────┘
 *   31                     16 15                      0
 *   ┌────────────────────────┬────────────────────────┐
 *   │      Base Low          │      Limit Low         │
 *   │      (16 bits)         │      (16 bits)         │
 *   └────────────────────────┴────────────────────────┘
 *
 * In 64-bit long mode:
 *   - Base and Limit are IGNORED for code/data segments (flat model)
 *   - Access byte still matters (DPL, present bit, type)
 *   - L bit (long mode) must be set for 64-bit code segments
 *
 * TSS in 64-bit Mode:
 *
 *   The TSS is simpler than in 32-bit mode. It mainly provides:
 *   - RSP0: Kernel stack for ring 0 (used on privilege escalation)
 *   - RSP1/RSP2: Unused (rings 1 and 2 not used)
 *   - IST1-IST7: Interrupt Stack Table for special handlers
 *
 *   The TSS descriptor in the GDT is 16 bytes (double-width) to hold
 *   the 64-bit base address.
 */

#include "gdt.h"
#include <stddef.h>

/*
 * GDT entry (8 bytes)
 *
 * In 64-bit mode, base and limit are ignored for code/data segments,
 * but we still need to set the access byte and flags correctly.
 */
struct gdt_entry {
    uint16_t limit_low;     /* Limit bits 0-15 */
    uint16_t base_low;      /* Base bits 0-15 */
    uint8_t  base_mid;      /* Base bits 16-23 */
    uint8_t  access;        /* Access byte */
    uint8_t  flags_limit;   /* Flags (4 bits) + Limit bits 16-19 */
    uint8_t  base_high;     /* Base bits 24-31 */
} __attribute__((packed));

/*
 * TSS descriptor (16 bytes in 64-bit mode)
 * 
 * The TSS descriptor is twice the size of a normal descriptor because
 * it needs to hold a 64-bit base address.
 */
struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit;
    uint8_t  base_high;
    uint32_t base_upper;    /* Upper 32 bits of base (64-bit extension) */
    uint32_t reserved;
} __attribute__((packed));

/*
 * Task State Segment (64-bit)
 *
 * In 64-bit mode, TSS doesn't store general registers like in 32-bit.
 * Its main purpose is providing RSP values for privilege level changes.
 */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;          /* Stack pointer for ring 0 (kernel stack) */
    uint64_t rsp1;          /* Stack pointer for ring 1 (unused) */
    uint64_t rsp2;          /* Stack pointer for ring 2 (unused) */
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt Stack Table entry 1 */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O Permission Bitmap offset */
} __attribute__((packed));

/* GDTR - pointer to GDT */
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/*
 * Access byte bits:
 *   7: Present (P) - must be 1
 *   6-5: DPL - privilege level (0=kernel, 3=user)
 *   4: Descriptor type (S) - 1 for code/data, 0 for system (TSS)
 *   3-0: Type
 *
 * Type for code segments: 0xA = execute/read
 * Type for data segments: 0x2 = read/write
 * Type for TSS: 0x9 = 64-bit TSS (available)
 */
#define ACCESS_PRESENT   (1 << 7)
#define ACCESS_DPL0      (0 << 5)
#define ACCESS_DPL3      (3 << 5)
#define ACCESS_CODE_DATA (1 << 4)
#define ACCESS_SYSTEM    (0 << 4)
#define ACCESS_CODE      0xA    /* Execute/Read */
#define ACCESS_DATA      0x2    /* Read/Write */
#define ACCESS_TSS64     0x9    /* 64-bit TSS (available) */

/* 
 * Flags (high nibble of flags_limit byte):
 *   7: Granularity (G) - 0=byte, 1=4KB
 *   6: Size (D/B) - must be 0 for 64-bit code
 *   5: Long mode (L) - 1 for 64-bit code
 *   4: Available (AVL)
 */
#define FLAG_LONG_MODE   (1 << 5)   /* L bit - 64-bit code segment */
#define FLAG_GRANULARITY (1 << 7)   /* G bit - 4KB granularity */

/* Our GDT: null + kernel code + kernel data + user code + user data + TSS */
#define GDT_ENTRIES 7   /* 5 regular entries + TSS takes 2 slots */

static struct gdt_entry gdt[GDT_ENTRIES];
static struct tss tss;
static struct gdtr gdtr;

/* =============================================================================
 * Internal Helper Functions
 * =============================================================================
 */

/*
 * gdt_set_entry - Configure a code/data segment descriptor.
 *
 * @index:  GDT entry index (0-based)
 * @access: Access byte (present, DPL, type bits)
 * @flags:  Flags (long mode, granularity)
 *
 * In 64-bit mode, base and limit are ignored for code/data segments.
 * We set them to 0/max for consistency.
 */
static void gdt_set_entry(int index, uint8_t access, uint8_t flags) {
    gdt[index].limit_low   = 0xFFFF;    /* Limit low (ignored in 64-bit) */
    gdt[index].base_low    = 0;         /* Base low (ignored in 64-bit) */
    gdt[index].base_mid    = 0;         /* Base middle (ignored in 64-bit) */
    gdt[index].access      = access;    /* Access byte: P, DPL, S, Type */
    gdt[index].flags_limit = flags | 0x0F;  /* Flags (high nibble) + limit high */
    gdt[index].base_high   = 0;         /* Base high (ignored in 64-bit) */
}

/*
 * gdt_set_tss - Configure the TSS descriptor (16 bytes, spans 2 GDT slots).
 *
 * @index: GDT entry index where TSS descriptor starts
 * @base:  Physical address of the TSS structure
 * @limit: Size of TSS minus 1
 *
 * The TSS descriptor is special in 64-bit mode:
 *   - It's 16 bytes instead of 8 (needs full 64-bit base address)
 *   - It's a "system" segment (S bit = 0)
 *   - Type = 0x9 (64-bit TSS available)
 */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit) {
    struct tss_descriptor *desc = (struct tss_descriptor *)&gdt[index];

    /* Lower 32 bits of base split across three fields */
    desc->limit_low   = limit & 0xFFFF;
    desc->base_low    = base & 0xFFFF;
    desc->base_mid    = (base >> 16) & 0xFF;
    desc->access      = ACCESS_PRESENT | ACCESS_SYSTEM | ACCESS_TSS64;
    desc->flags_limit = (limit >> 16) & 0x0F;
    desc->base_high   = (base >> 24) & 0xFF;

    /* Upper 32 bits of base (64-bit extension) */
    desc->base_upper  = (base >> 32) & 0xFFFFFFFF;
    desc->reserved    = 0;
}

/* =============================================================================
 * Assembly Helpers (defined in gdt_asm.S)
 *
 * These are implemented in assembly because:
 *   - LGDT instruction requires specific operand format
 *   - Segment register reload requires far jump for CS
 * =============================================================================
 */
extern void gdt_load(struct gdtr *gdtr, uint16_t code_sel, uint16_t data_sel);
extern void tss_load(uint16_t tss_sel);

/* =============================================================================
 * GDT/TSS Public API
 * =============================================================================
 */

/*
 * gdt_init - Set up the GDT and TSS, then load them into the CPU.
 *
 * This function:
 *   1. Creates descriptors for kernel/user code/data segments
 *   2. Creates the TSS descriptor
 *   3. Loads the GDT via LGDT
 *   4. Reloads segment registers with new selectors
 *   5. Loads the TSS via LTR
 */
void gdt_init(void) {
    /* Entry 0: Null descriptor (required by x86 architecture) */
    gdt[0] = (struct gdt_entry){0};

    /* Entry 1: Kernel code segment (selector 0x08) */
    gdt_set_entry(1, 
        ACCESS_PRESENT | ACCESS_DPL0 | ACCESS_CODE_DATA | ACCESS_CODE,
        FLAG_LONG_MODE);

    /* Entry 2: Kernel data segment (selector 0x10) */
    gdt_set_entry(2,
        ACCESS_PRESENT | ACCESS_DPL0 | ACCESS_CODE_DATA | ACCESS_DATA,
        0);

    /* Entry 3: User code segment (selector 0x18) */
    gdt_set_entry(3,
        ACCESS_PRESENT | ACCESS_DPL3 | ACCESS_CODE_DATA | ACCESS_CODE,
        FLAG_LONG_MODE);

    /* Entry 4: User data segment (selector 0x20) */
    gdt_set_entry(4,
        ACCESS_PRESENT | ACCESS_DPL3 | ACCESS_CODE_DATA | ACCESS_DATA,
        0);

    /* Entry 5-6: TSS (selector 0x28, spans 16 bytes) */
    /* Initialize TSS structure */
    for (int i = 0; i < (int)sizeof(tss); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }
    tss.iopb_offset = sizeof(tss);  /* No I/O permission bitmap */
    
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss) - 1);

    /* Set up GDTR */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;

    /* Load GDT and reload segment registers */
    gdt_load(&gdtr, GDT_KERNEL_CODE, GDT_KERNEL_DATA);

    /* Load TSS */
    tss_load(GDT_TSS);
}

/*
 * tss_set_kernel_stack - Update RSP0 in the TSS.
 *
 * @stack: New kernel stack pointer value.
 *
 * RSP0 is loaded by the CPU when transitioning from ring 3 to ring 0.
 * Each process should have its own kernel stack, and this function
 * should be called during context switch to set the correct stack.
 */
void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}
