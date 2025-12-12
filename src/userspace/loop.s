.globl _start
_start:
    # Print message first
    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea message(%rip), %rsi
    mov $24, %rdx         # length
    int $0x80

    # Infinite loop
spin:
    jmp spin

message:
    .ascii "Looping forever.......\n"
