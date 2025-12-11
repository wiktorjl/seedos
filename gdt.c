/*
 * gdt.c - Global Descriptor Table and Task State Segment
 *
 * In 64-bit long mode, segmentation is mostly disabled. The CPU ignores
 * base and limit for code/data segments. However, we still need:
 *   - Proper DPL (privilege level) in segment descriptors
 *   - TSS for RSP0 (kernel stack when entering from ring 3)
 *   - Separate selectors for kernel (ring 0) and user (ring 3)
 */

#include "gdt.h"
#include "console.h"
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

/*
 * Set a GDT entry (for code/data segments)
 */
static void gdt_set_entry(int index, uint8_t access, uint8_t flags) {
    gdt[index].limit_low   = 0xFFFF;    /* Limit (ignored in 64-bit) */
    gdt[index].base_low    = 0;
    gdt[index].base_mid    = 0;
    gdt[index].access      = access;
    gdt[index].flags_limit = flags | 0x0F;  /* Flags + limit high nibble */
    gdt[index].base_high   = 0;
}

/*
 * Set the TSS descriptor (spans 2 GDT slots)
 */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit) {
    struct tss_descriptor *desc = (struct tss_descriptor *)&gdt[index];
    
    desc->limit_low   = limit & 0xFFFF;
    desc->base_low    = base & 0xFFFF;
    desc->base_mid    = (base >> 16) & 0xFF;
    desc->access      = ACCESS_PRESENT | ACCESS_SYSTEM | ACCESS_TSS64;
    desc->flags_limit = (limit >> 16) & 0x0F;
    desc->base_high   = (base >> 24) & 0xFF;
    desc->base_upper  = (base >> 32) & 0xFFFFFFFF;
    desc->reserved    = 0;
}

/*
 * Assembly helpers to load GDT and segment registers
 */
extern void gdt_load(struct gdtr *gdtr, uint16_t code_sel, uint16_t data_sel);
extern void tss_load(uint16_t tss_sel);

void gdt_init(void) {
    puts("Setting up GDT...\n");

    /* Entry 0: Null descriptor (required) */
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

    puts("  GDTR base: ");
    put_hex(gdtr.base);
    puts("\n  GDTR limit: ");
    put_hex(gdtr.limit);
    puts("\n");

    /* Load GDT and reload segment registers */
    gdt_load(&gdtr, GDT_KERNEL_CODE, GDT_KERNEL_DATA);

    puts("  GDT loaded, reloaded segment registers\n");

    /* Load TSS */
    tss_load(GDT_TSS);

    puts("  TSS loaded\n");
    puts("GDT setup complete!\n\n");
}

void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}
