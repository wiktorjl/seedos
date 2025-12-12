.globl _start
_start:
    # Print message first
    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea message(%rip), %rsi
    mov $19, %rdx         # length
    int $0x80

    # Crash by accessing invalid memory
    mov $0xDEADBEEF, %rax
    mov (%rax), %rbx      # Page fault!

message:
    .ascii "About to crash...\n"
