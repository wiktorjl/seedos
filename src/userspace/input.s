# input.s - Tests sys_read by reading and echoing keyboard input
.globl _start
_start:
    # Print prompt
    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea prompt(%rip), %rsi
    mov $14, %rdx         # length
    int $0x80

read_loop:
    # sys_read(0, buffer, 1) - read one char from stdin
    mov $2, %rax          # SYS_READ
    mov $0, %rdi          # fd = stdin
    lea buffer(%rip), %rsi
    mov $1, %rdx          # count = 1
    int $0x80

    # Check if we read anything
    cmp $0, %rax
    je read_loop          # No input yet, keep polling

    # Check for 'q' to quit
    movzbl buffer(%rip), %eax
    cmp $'q', %al
    je done

    # Check for Enter key
    cmp $'\n', %al
    je newline

    # Echo the character back
    mov $1, %rax          # SYS_WRITE
    mov $1, %rdi          # fd = stdout
    lea buffer(%rip), %rsi
    mov $1, %rdx
    int $0x80

    jmp read_loop

newline:
    # Print newline
    mov $1, %rax
    mov $1, %rdi
    lea nl(%rip), %rsi
    mov $1, %rdx
    int $0x80
    jmp read_loop

done:
    # Print goodbye message
    mov $1, %rax
    mov $1, %rdi
    lea goodbye(%rip), %rsi
    mov $9, %rdx
    int $0x80

    # Exit
    mov $0, %rax          # SYS_EXIT
    mov $0, %rdi
    int $0x80

.section .rodata
prompt:
    .ascii "Type (q=quit): "
goodbye:
    .ascii "\nBye!\n"
nl:
    .ascii "\n"

.section .data
buffer:
    .byte 0
