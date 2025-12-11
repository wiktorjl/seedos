.section .note.GNU-stack, "", @progbits
.section .bss
.align 16
stack_bottom:
    .space 16384
stack_top:

.section .text
.global _start
_start:
    leaq stack_top(%rip), %rsp
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
