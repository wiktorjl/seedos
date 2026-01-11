# Minimal C Runtime Startup
#
# _start is the entry point from the kernel.
# Stack is already set up with: argc, argv[], envp[], auxv[]
#
# This code calls main(argc, argv) and exits with the return value.

.global _start
.section .text

_start:
    # Clear frame pointer for clean backtraces
    xorl    %ebp, %ebp

    # Get argc from stack
    # Stack layout: [argc] [argv[0]] [argv[1]] ... [NULL] [envp[0]] ...
    movq    (%rsp), %rdi        # argc = first thing on stack

    # Calculate argv = rsp + 8
    leaq    8(%rsp), %rsi       # argv = &stack[1]

    # Align stack to 16 bytes (ABI requirement before call)
    andq    $-16, %rsp

    # Call main(argc, argv)
    call    main

    # Exit with main's return value
    # Return value is in %eax, move to %edi for exit syscall
    movl    %eax, %edi
    movl    $60, %eax           # SYS_exit = 60
    syscall

    # Should never reach here
    ud2
