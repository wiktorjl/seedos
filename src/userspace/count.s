# count.s - Counts from 0 to 9, printing each digit
.globl _start
_start:
    mov $0, %r12          # counter = 0

loop:
    # Print the digit (ASCII '0' + counter)
    mov %r12, %rax
    add $48, %al          # Convert to ASCII
    mov %al, digit(%rip)  # Store in buffer

    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea digit(%rip), %rsi
    mov $1, %rdx          # length = 1
    int $0x80

    inc %r12
    cmp $10, %r12
    jl loop

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

digit:
    .byte 0
newline:
    .ascii "\n"
