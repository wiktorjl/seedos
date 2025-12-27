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

    .text : {                        /* Code section */
        *(.text .text.*)             /* Include all .text sections from all input files */
    }
}