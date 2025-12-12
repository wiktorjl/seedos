# alpha.s - Prints A-Z alphabet
.globl _start
_start:
    mov $65, %r12         # 'A' = 65

loop:
    # Print the letter
    mov %r12, %rax
    mov %al, letter(%rip)

    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea letter(%rip), %rsi
    mov $1, %rdx          # length = 1
    int $0x80

    inc %r12
    cmp $91, %r12         # 'Z' + 1 = 91
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

letter:
    .byte 0
newline:
    .ascii "\n"
