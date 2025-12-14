/*
 * syscall.h - System Call Interface
 *
 * System calls are the interface between user programs and the kernel.
 * They allow unprivileged code to request privileged operations.
 *
 * System Call Mechanism (INT 0x80):
 *
 *   This OS uses the traditional Linux INT 0x80 interface:
 *
 *   1. User program loads syscall number into RAX
 *   2. Arguments go in RDI, RSI, RDX, RCX, R8, R9 (like function calls)
 *   3. User executes INT 0x80
 *   4. CPU traps to kernel via IDT entry 128 (0x80)
 *   5. Kernel reads registers, performs operation
 *   6. Return value placed in RAX
 *   7. IRETQ returns to user code
 *
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  User Mode (Ring 3)                                     │
 *   │    mov rax, SYS_WRITE    ; syscall number               │
 *   │    mov rdi, 1            ; fd = stdout                  │
 *   │    mov rsi, buffer       ; buffer address               │
 *   │    mov rdx, length       ; byte count                   │
 *   │    int 0x80              ; trap to kernel               │
 *   └─────────────────────────────────────────────────────────┘
 *                              │
 *                              ▼
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  Kernel Mode (Ring 0)                                   │
 *   │    syscall_handler() reads registers and dispatches     │
 *   └─────────────────────────────────────────────────────────┘
 *
 * Currently Implemented Syscalls:
 *
 *   Number  Name       Arguments                    Description
 *   ──────  ─────────  ───────────────────────────  ────────────────────
 *   0       SYS_EXIT   rdi=exit_code                Terminate process
 *   1       SYS_WRITE  rdi=fd, rsi=buf, rdx=count   Write to file descriptor
 *
 * Note: Modern Linux uses SYSCALL/SYSRET instead of INT 0x80 for better
 * performance. INT 0x80 is simpler to implement and sufficient for learning.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

/* =============================================================================
 * System Call Numbers
 *
 * These numbers are placed in RAX before executing INT 0x80.
 * =============================================================================
 */
  #define SYS_EXIT    0  /* Exit process: rdi=exit_code */
  #define SYS_WRITE   1  /* Write: rdi=fd, rsi=buffer, rdx=count -> bytes written */
  #define SYS_READ    2  /* Read: rdi=fd, rsi=buffer, rdx=count -> bytes read */
  #define SYS_GETPID  3  /* Get process ID: -> pid */
  #define SYS_UPTIME  4  /* Get uptime in seconds: -> uptime */
  #define SYS_SBRK    5  /* Change data segment size: rdi=increment -> old top */
  #define SYS_OPEN    6  /* Open file: rdi=path, rsi=flags -> fd */
  #define SYS_CLOSE   7  /* Close file: rdi=fd -> 0 on success */
  #define SYS_LSEEK   8  /* Seek: rdi=fd, rsi=offset, rdx=whence -> new position */
  #define SYS_STAT    9  /* Stat: rdi=path, rsi=buf -> 0 on success */
  #define SYS_FSTAT  10  /* Fstat: rdi=fd, rsi=buf -> 0 on success */
  #define SYS_GETDENTS 11 /* Getdents: rdi=fd, rsi=buf, rdx=count -> bytes read */
  #define SYS_GETCWD 12  /* Getcwd: rdi=buf, rsi=size -> buf or NULL */
  #define SYS_CHDIR  13  /* Chdir: rdi=path -> 0 on success */
  #define SYS_ISATTY 14  /* Isatty: rdi=fd -> 1 if tty, 0 otherwise */
  #define SYS_DUP    15  /* Dup: rdi=oldfd -> new fd */
  #define SYS_DUP2   16  /* Dup2: rdi=oldfd, rsi=newfd -> newfd */
  #define SYS_SPAWN  17  /* Spawn: rdi=path, rsi=argv -> exit code */
  #define SYS_SPAWN_ASYNC 18 /* Spawn async: rdi=path, rsi=argv -> child pid */
  #define SYS_WAITPID 19  /* Wait for child: rdi=pid -> exit code */
  #define SYS_SHUTDOWN 20 /* Shutdown: halt the system */
  #define SYS_REBOOT   21 /* Reboot: restart the system */


/* =============================================================================
 * Syscall Register Context
 *
 * When the syscall handler is called, registers are saved on the stack.
 * This structure provides access to the saved values.
 * =============================================================================
 */
struct syscall_registers {
    /* Saved registers (in reverse push order) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;  /* Argument 1: fd / exit_code */
    uint64_t rsi;  /* Argument 2: buffer pointer */
    uint64_t rdx;  /* Argument 3: byte count */
    uint64_t rcx;  /* Argument 4 (not currently used) */
    uint64_t rbx;  /* Preserved across syscall */
    uint64_t rax;  /* Syscall number (input), return value (output) */
};

/* =============================================================================
 * Syscall Handler
 * =============================================================================
 */

/*
 * syscall_handler - Main entry point for all system calls.
 *
 * @regs: Pointer to saved register state from the INT 0x80 trap.
 *
 * Dispatches to the appropriate syscall implementation based on RAX.
 * The return value (if any) is placed back in regs->rax.
 *
 * Called from the INT 0x80 assembly stub in isr.S.
 */
void syscall_handler(struct syscall_registers *regs);

#endif /* SYSCALL_H */