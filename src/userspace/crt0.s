/*
 * crt0.s - C Runtime Startup Code
 *
 * This is the entry point for C userspace programs. The kernel sets up
 * the stack with argc/argv before jumping here:
 *
 *   [strings...]    <- high addresses (argv string data)
 *   [NULL]          <- argv terminator
 *   [argv[n-1] ptr]
 *   ...
 *   [argv[0] ptr]
 *   [argc]          <- RSP points here on entry
 *
 * We pop argc into RDI, set argv (RSP) into RSI, align the stack,
 * call main(argc, argv), then exit with main's return value.
 */

.section .text
.globl _start
_start:
    /* Pop argc into RDI (first argument to main) */
    pop %rdi

    /* RSP now points to argv[0], which is the start of the argv array */
    mov %rsp, %rsi

    /* Align stack to 16 bytes (System V AMD64 ABI requirement) */
    and $-16, %rsp

    /* Call main(argc, argv) */
    call main

    /* main() returned, exit with its return value (in RAX) */
    mov %rax, %rdi      /* exit code = return value from main */
    mov $0, %rax        /* SYS_EXIT = 0 */
    int $0x80

    /* Should never reach here, but just in case... */
.loop:
    jmp .loop
