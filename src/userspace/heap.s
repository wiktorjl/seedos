# heap.s - Tests sbrk syscall by allocating and freeing memory
.globl _start
_start:
    # Print "Allocating 16 bytes...\n"
    mov $1, %rax
    mov $1, %rdi
    lea msg1(%rip), %rsi
    mov $23, %rdx
    int $0x80

    # sys_sbrk(16) - allocate 16 bytes
    mov $5, %rax          # SYS_SBRK
    mov $16, %rdi         # increment = 16
    int $0x80
    mov %rax, %r12        # save returned pointer

    # Check if allocation failed
    cmp $-1, %rax
    je fail

    # Write 'ABCD' to the allocated memory
    movb $65, (%r12)      # 'A'
    movb $66, 1(%r12)     # 'B'
    movb $67, 2(%r12)     # 'C'
    movb $68, 3(%r12)     # 'D'

    # Print "Write OK\n"
    mov $1, %rax
    mov $1, %rdi
    lea msg2(%rip), %rsi
    mov $9, %rdx
    int $0x80

    # Read back and print
    mov $1, %rax
    mov $1, %rdi
    mov %r12, %rsi        # buffer = our allocated memory
    mov $4, %rdx          # print 4 chars
    int $0x80

    # Print newline
    mov $1, %rax
    mov $1, %rdi
    lea newline(%rip), %rsi
    mov $1, %rdx
    int $0x80

    # Print "Freeing 16 bytes...\n"
    mov $1, %rax
    mov $1, %rdi
    lea msg_free(%rip), %rsi
    mov $20, %rdx
    int $0x80

    # sys_sbrk(-16) - free 16 bytes
    mov $5, %rax          # SYS_SBRK
    mov $-16, %rdi        # increment = -16
    int $0x80

    # Check if free failed
    cmp $-1, %rax
    je fail

    # Print "Free OK\n"
    mov $1, %rax
    mov $1, %rdi
    lea msg_freeok(%rip), %rsi
    mov $8, %rdx
    int $0x80

    # Print "Success!\n"
    mov $1, %rax
    mov $1, %rdi
    lea msg3(%rip), %rsi
    mov $9, %rdx
    int $0x80

    # Exit with success
    mov $0, %rax
    mov $0, %rdi
    int $0x80

fail:
    # Print "Failed!\n"
    mov $1, %rax
    mov $1, %rdi
    lea msg_fail(%rip), %rsi
    mov $8, %rdx
    int $0x80

    # Exit with error
    mov $0, %rax
    mov $1, %rdi
    int $0x80

.section .rodata
msg1:
    .ascii "Allocating 16 bytes...\n"
msg2:
    .ascii "Write OK\n"
msg3:
    .ascii "Success!\n"
msg_fail:
    .ascii "Failed!\n"
msg_free:
    .ascii "Freeing 16 bytes...\n"
msg_freeok:
    .ascii "Free OK\n"
newline:
    .ascii "\n"
