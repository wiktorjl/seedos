.global kthread_switch
.type kthread_switch, @function

# void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp)
#
# Arguments (System V AMD64 ABI):
#   rdi = pointer to old thread's RSP storage (old_rsp)
#   rsi = new thread's RSP value (new_rsp)
#
# This function:
#   1. Saves callee-saved registers onto current stack
#   2. Saves current RSP to *old_rsp
#   3. Loads new RSP from new_rsp
#   4. Restores callee-saved registers from new stack
#   5. Returns (to wherever the new thread left off)
#
# Stack layout (must match kthread_create):
#   RSP+48 → return address
#   RSP+40 → R15
#   RSP+32 → R14
#   RSP+24 → R13
#   RSP+16 → R12
#   RSP+8  → RBP
#   RSP    → RBX

kthread_switch:
    # Save callee-saved registers onto current stack
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    # Save current RSP to *old_rsp (rdi points to storage location)
    mov %rsp, (%rdi)

    # Load new RSP
    mov %rsi, %rsp

    # Restore callee-saved registers from new stack
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    ret
