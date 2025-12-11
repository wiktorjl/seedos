/*
 * gdt.h - Global Descriptor Table (GDT) and Task State Segment (TSS)
 *
 * The GDT defines memory segments and privilege levels. In 64-bit long mode,
 * segmentation is largely disabled (flat memory model), but we still need:
 *
 *   1. Segment selectors with proper DPL (Descriptor Privilege Level)
 *   2. TSS for kernel stack pointer during ring 3 -> ring 0 transitions
 *
 * x86 Protection Rings:
 *
 *   ┌─────────────────────────────────────┐
 *   │            Ring 0 (Kernel)          │  Most privileged
 *   │  ┌───────────────────────────────┐  │  - Full hardware access
 *   │  │         Ring 1 (Unused)       │  │  - Can execute privileged instructions
 *   │  │  ┌─────────────────────────┐  │  │
 *   │  │  │      Ring 2 (Unused)    │  │  │
 *   │  │  │  ┌───────────────────┐  │  │  │
 *   │  │  │  │  Ring 3 (User)    │  │  │  │  Least privileged
 *   │  │  │  │   Applications    │  │  │  │  - Limited I/O access
 *   │  │  │  └───────────────────┘  │  │  │  - Cannot access kernel memory
 *   │  │  └─────────────────────────┘  │  │
 *   │  └───────────────────────────────┘  │
 *   └─────────────────────────────────────┘
 *
 * Segment Selector Format (16 bits):
 *
 *   15                  3  2  1  0
 *   ┌───────────────────┬───┬────┐
 *   │      Index        │TI │ RPL│
 *   └───────────────────┴───┴────┘
 *
 *   Index: GDT entry number (multiply by 8 to get byte offset)
 *   TI: Table Indicator (0 = GDT, 1 = LDT)
 *   RPL: Requested Privilege Level (0-3)
 *
 *   Example: Selector 0x08 = Index 1, TI=0, RPL=0 (GDT entry 1, ring 0)
 *
 * Our GDT Layout:
 *
 *   Index  Selector  Description           DPL
 *   ─────  ────────  ────────────────────  ───
 *     0    0x00      Null descriptor       -
 *     1    0x08      Kernel code (64-bit)  0
 *     2    0x10      Kernel data           0
 *     3    0x18      User code (64-bit)    3
 *     4    0x20      User data             3
 *     5    0x28      TSS (16 bytes)        0
 *
 * Why TSS is Needed:
 *
 *   When transitioning from ring 3 to ring 0 (e.g., syscall or interrupt),
 *   the CPU needs to switch to a kernel stack. It reads RSP0 from the TSS.
 *   Without proper RSP0, the CPU would use the user's stack for kernel
 *   operations - a security disaster!
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* =============================================================================
 * Segment Selectors
 *
 * These are the values loaded into segment registers (CS, DS, SS, etc.).
 * Each selector is: (GDT index * 8) | RPL
 * =============================================================================
 */
#define GDT_KERNEL_CODE  0x08  /* Index 1, RPL 0: Kernel code segment */
#define GDT_KERNEL_DATA  0x10  /* Index 2, RPL 0: Kernel data segment */
#define GDT_USER_CODE    0x18  /* Index 3, RPL 0: User code segment (DPL=3) */
#define GDT_USER_DATA    0x20  /* Index 4, RPL 0: User data segment (DPL=3) */
#define GDT_TSS          0x28  /* Index 5, RPL 0: Task State Segment */

/*
 * SELECTOR_RPL3 - Set RPL (Requested Privilege Level) to 3.
 *
 * When loading a segment selector, the RPL indicates the privilege level
 * of the code making the request. For user-mode code, this must be 3.
 *
 * Example: User code segment selector for IRETQ:
 *   CS = GDT_USER_CODE | 3 = 0x18 | 3 = 0x1B
 */
#define SELECTOR_RPL3(sel)  ((sel) | 3)

/* =============================================================================
 * GDT/TSS API Functions
 * =============================================================================
 */

/*
 * gdt_init - Initialize the Global Descriptor Table and TSS.
 *
 * Sets up:
 *   - Null descriptor (required, index 0)
 *   - Kernel code/data segments (ring 0)
 *   - User code/data segments (ring 3)
 *   - TSS descriptor
 *
 * Then loads the GDT via LGDT and the TSS via LTR.
 * Must be called early in boot, before enabling interrupts.
 */
void gdt_init(void);

/*
 * tss_set_kernel_stack - Set the kernel stack pointer in TSS.
 *
 * @stack: The kernel stack pointer to use for ring transitions.
 *
 * When an interrupt or syscall occurs in user mode, the CPU:
 *   1. Reads RSP0 from the TSS
 *   2. Switches to this kernel stack
 *   3. Pushes the interrupt frame
 *
 * This should be called when switching processes to set each process's
 * kernel stack (typically the top of their kernel stack page).
 */
void tss_set_kernel_stack(uint64_t stack);

#endif /* GDT_H */
