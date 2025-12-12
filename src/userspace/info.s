# info.s - Prints PID and uptime using new syscalls
.globl _start
_start:
    # Print "PID: "
    mov $1, %rax
    mov $1, %rdi
    lea pid_label(%rip), %rsi
    mov $5, %rdx
    int $0x80

    # sys_getpid()
    mov $3, %rax          # SYS_GETPID
    int $0x80
    mov %rax, %r12        # save pid

    # Print the PID digit (assumes single digit for now)
    add $48, %r12         # ASCII
    mov %r12b, digit(%rip)
    mov $1, %rax
    mov $1, %rdi
    lea digit(%rip), %rsi
    mov $1, %rdx
    int $0x80

    # Print newline
    mov $1, %rax
    mov $1, %rdi
    lea newline(%rip), %rsi
    mov $1, %rdx
    int $0x80

    # Print "Uptime: "
    mov $1, %rax
    mov $1, %rdi
    lea uptime_label(%rip), %rsi
    mov $8, %rdx
    int $0x80

    # sys_uptime()
    mov $4, %rax          # SYS_UPTIME
    int $0x80
    # Result in rax - just print "ok" for now (full number printing is complex)

    # Print "ms\n"
    mov $1, %rax
    mov $1, %rdi
    lea ms_label(%rip), %rsi
    mov $4, %rdx
    int $0x80

    # Exit
    mov $0, %rax
    mov $0, %rdi
    int $0x80

pid_label:
    .ascii "PID: "
uptime_label:
    .ascii "Uptime: "
ms_label:
    .ascii "ok\n\n"
digit:
    .byte 0
newline:
    .ascii "\n"
