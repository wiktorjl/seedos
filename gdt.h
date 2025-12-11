/*
 * gdt.h - Global Descriptor Table
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Segment selectors */
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20
#define GDT_TSS          0x28

/* Selector with RPL (Requested Privilege Level) */
#define SELECTOR_RPL3(sel)  ((sel) | 3)

/*
 * Initialize the GDT and TSS.
 * Must be called early in boot, before entering userspace.
 */
void gdt_init(void);

/*
 * Set the kernel stack pointer in TSS.
 * Called when switching processes to set RSP0.
 */
void tss_set_kernel_stack(uint64_t stack);

#endif /* GDT_H */
