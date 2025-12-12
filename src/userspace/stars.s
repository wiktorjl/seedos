# stars.s - Prints 20 asterisks
.globl _start
_start:
    mov $20, %r12         # counter = 20

loop:
    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea star(%rip), %rsi
    mov $1, %rdx          # length = 1
    int $0x80

    dec %r12
    jnz loop

    # Print newline
    mov $1, %rax
    mov $1, %rdi
    lea newline(%rip), %rsi
    mov $1, %rdx
    int $0x80

    # Exit
    mov $0, %rax          # SYS_EXIT
    mov $0, %rdi
    int $0x80

star:
    .ascii "*"
newline:
    .ascii "\n"
