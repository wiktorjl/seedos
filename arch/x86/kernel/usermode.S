# SPDX-License-Identifier: GPL-2.0-only
#
# usermode.S - Enter user mode from kernel
#
# Provides user_mode_enter() to transfer control to Ring 3.
# Uses iretq to atomically switch privilege levels.
#

# GDT segment selectors (must match gdt.h)
# GDT_USER_DATA = 0x18, GDT_USER_CODE = 0x20
# With RPL=3: USER_SS = 0x1B, USER_CS = 0x23
.set USER_CS, 0x23
.set USER_SS, 0x1B

.section .text
.global user_mode_enter

#
# user_mode_enter - Enter user mode
#
# void user_mode_enter(uint64_t entry, uint64_t user_rsp)
#
# Parameters:
#   rdi = entry point (user RIP)
#   rsi = user stack pointer (user RSP)
#
# This function does not return - it transfers to Ring 3.
#
# We build an iret frame and execute iretq to transition to user mode.
# The iret frame format (from top of stack down):
#   SS      (user stack segment)
#   RSP     (user stack pointer)
#   RFLAGS  (user flags with IF=1)
#   CS      (user code segment)
#   RIP     (user instruction pointer)
#
user_mode_enter:
    # Disable interrupts during transition
    cli

    # Switch GS base - swap kernel GS with user GS
    # After swapgs: GS_BASE = user gs_base, KERNEL_GS_BASE = kernel percpu
    swapgs

    # Build iret frame on current (kernel) stack
    # Push in reverse order since stack grows down

    pushq $USER_SS          # SS (user data segment, RPL=3)
    pushq %rsi              # RSP (user stack pointer from parameter)
    pushq $0x202            # RFLAGS (IF=1 for interrupts enabled, reserved bit 1 set)
    pushq $USER_CS          # CS (user code segment, RPL=3)
    pushq %rdi              # RIP (entry point from parameter)

    # Clear all general-purpose registers for clean entry
    # This prevents leaking kernel data to userspace
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    # Transfer to Ring 3
    # iretq pops: RIP, CS, RFLAGS, RSP, SS
    # and atomically switches to user mode
    iretq

    # Should never reach here
    ud2
