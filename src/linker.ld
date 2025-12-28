/*
 * Linker script for kernel.elf
 *
 * The linker script tells ld how to combine object files into the final
 * executable. It controls where sections are placed in memory and what
 * virtual addresses they get. Limine reads the ELF headers and loads
 * the kernel at these addresses before jumping to _start.
 */

ENTRY(_start)                       /* Tell the ELF header that _start is the entry point */

SECTIONS
{
    . = 0xFFFFFFFF80000000;          /* Start at higher-half address (top 2GB of 64-bit space) */
                                     /* This is conventional for kernels - user space gets lower addresses */

    .text : {                        /* Code section (read + execute) */
        *(.text .text.*)
    }

    . = ALIGN(4096);                 /* Page-align to get separate segment */

    .limine_requests : {             /* Limine boot protocol requests */
        *(.limine_requests_start)    /* Start marker must come first */
        *(.limine_requests)          /* Actual requests */
        *(.limine_requests_end)      /* End marker must come last */
    }

    .rodata : {                      /* Read-only data (font, strings, etc.) */
        *(.rodata .rodata.*)
    }

    .data : {                        /* Initialized data (read + write) */
        *(.data .data.*)
    }
}