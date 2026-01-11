/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * x86-64 Global Descriptor Table
 *
 * Defines segment descriptors for kernel and user mode.
 * In long mode, most segment fields are ignored except:
 *   - Code segment: L bit (long mode), D bit (must be 0 for 64-bit)
 *   - All segments: DPL (privilege level), P (present)
 *   - TSS: base address and limit are still used
 */

#ifndef _GDT_H
#define _GDT_H

#include "types.h"

/*
 * Segment Selectors
 *
 * Format: Index (13 bits) | TI (1 bit) | RPL (2 bits)
 * For GDT entries: selector = index * 8
 *
 * IMPORTANT: User Data must come before User Code for SYSRET compatibility.
 * SYSRET loads: SS = STAR[63:48] + 8, CS = STAR[63:48] + 16
 * With STAR[63:48] = 0x10: SS = 0x18 (User Data), CS = 0x20 (User Code)
 */
#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

/* Number of GDT entries (TSS takes 2 slots) */
#define GDT_ENTRIES     7

/*
 * Access byte flags
 *
 * Bit 7: Present (P)
 * Bit 6-5: Descriptor Privilege Level (DPL)
 * Bit 4: Descriptor type (1 = code/data, 0 = system)
 * Bit 3: Executable (1 = code, 0 = data)
 * Bit 2: Direction/Conforming
 * Bit 1: Readable (code) / Writable (data)
 * Bit 0: Accessed
 */
#define GDT_ACCESS_PRESENT   (1 << 7)
#define GDT_ACCESS_DPL0      (0 << 5)
#define GDT_ACCESS_DPL3      (3 << 5)
#define GDT_ACCESS_CODE_DATA (1 << 4)
#define GDT_ACCESS_EXEC      (1 << 3)
#define GDT_ACCESS_RW        (1 << 1)

/* Kernel code: Present, DPL=0, Code/Data, Executable, Readable */
#define GDT_ACCESS_KERNEL_CODE (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXEC | \
                                GDT_ACCESS_RW)  /* 0x9A */

/* Kernel data: Present, DPL=0, Code/Data, Writable */
#define GDT_ACCESS_KERNEL_DATA (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW)  /* 0x92 */

/* User code: Present, DPL=3, Code/Data, Executable, Readable */
#define GDT_ACCESS_USER_CODE   (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | \
                                GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXEC | \
                                GDT_ACCESS_RW)  /* 0xFA */

/* User data: Present, DPL=3, Code/Data, Writable */
#define GDT_ACCESS_USER_DATA   (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | \
                                GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW)  /* 0xF2 */

/* TSS: Present, DPL=0, System segment, type 0x9 (64-bit TSS available) */
#define GDT_ACCESS_TSS         (GDT_ACCESS_PRESENT | 0x09)  /* 0x89 */

/*
 * Flags nibble (upper 4 bits of limit_flags byte)
 *
 * Bit 7: Granularity (0 = byte, 1 = 4KB pages)
 * Bit 6: Size (0 = 16-bit, 1 = 32-bit) - must be 0 for 64-bit code
 * Bit 5: Long mode (1 = 64-bit code segment)
 * Bit 4: Reserved
 */
#define GDT_FLAG_GRANULARITY (1 << 7)
#define GDT_FLAG_SIZE_32     (1 << 6)
#define GDT_FLAG_LONG_MODE   (1 << 5)

/* 64-bit code segment flags: Long mode, no 32-bit size */
#define GDT_FLAGS_CODE64     GDT_FLAG_LONG_MODE  /* 0x20 */

/* Data segment flags: none needed for 64-bit */
#define GDT_FLAGS_DATA       0x00

/*
 * GDT Entry (8 bytes)
 *
 * Standard segment descriptor format. In 64-bit mode, base and limit
 * are ignored for code/data segments, but the structure is preserved.
 */
typedef struct {
    uint16_t limit_low;      /* Limit bits 0-15 */
    uint16_t base_low;       /* Base bits 0-15 */
    uint8_t  base_mid;       /* Base bits 16-23 */
    uint8_t  access;         /* Access byte */
    uint8_t  limit_flags;    /* Limit 16-19 (low nibble) + Flags (high nibble) */
    uint8_t  base_high;      /* Base bits 24-31 */
} __attribute__((packed)) gdt_entry_t;

/*
 * TSS Descriptor (16 bytes)
 *
 * In 64-bit mode, TSS descriptor is 16 bytes to hold full 64-bit base.
 * This takes two GDT slots.
 */
typedef struct {
    uint16_t limit_low;      /* Limit bits 0-15 */
    uint16_t base_low;       /* Base bits 0-15 */
    uint8_t  base_mid;       /* Base bits 16-23 */
    uint8_t  access;         /* Access byte (0x89 for available TSS) */
    uint8_t  limit_flags;    /* Limit 16-19 + flags */
    uint8_t  base_mid2;      /* Base bits 24-31 */
    uint32_t base_high;      /* Base bits 32-63 */
    uint32_t reserved;       /* Must be zero */
} __attribute__((packed)) gdt_tss_entry_t;

/*
 * GDTR - GDT Register structure for lgdt instruction
 */
typedef struct {
    uint16_t limit;          /* Size of GDT - 1 */
    uint64_t base;           /* Linear address of GDT */
} __attribute__((packed)) gdtr_t;

/*
 * TSS - Task State Segment (104 bytes minimum)
 *
 * In 64-bit mode, TSS is used only for:
 *   - RSP0-RSP2: Stack pointers for privilege level changes
 *   - IST1-IST7: Interrupt Stack Table for specific interrupt handlers
 *   - I/O Permission Bitmap offset
 */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;           /* Stack pointer for Ring 0 (kernel) */
    uint64_t rsp1;           /* Stack pointer for Ring 1 (unused) */
    uint64_t rsp2;           /* Stack pointer for Ring 2 (unused) */
    uint64_t reserved1;
    uint64_t ist1;           /* Interrupt Stack Table entries */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;    /* I/O Permission Bitmap offset from TSS base */
} __attribute__((packed)) x86_tss_t;

/**
 * gdt_init - Initialize and load the GDT
 *
 * Sets up the GDT with kernel and user segments, initializes the TSS,
 * and loads both with lgdt and ltr instructions.
 */
void gdt_init(void);

/**
 * gdt_set_tss_rsp0 - Update the kernel stack pointer in TSS
 * @rsp0: New kernel stack pointer for Ring 3 -> Ring 0 transitions
 *
 * Called during context switch to set the kernel stack for the new process.
 * When a user process makes a syscall or takes an interrupt, the CPU will
 * load RSP from TSS.rsp0.
 */
void gdt_set_tss_rsp0(uint64_t rsp0);

/**
 * gdt_get_tss - Get pointer to the TSS
 *
 * Returns pointer to the kernel TSS structure.
 */
x86_tss_t *gdt_get_tss(void);

#endif /* _GDT_H */
