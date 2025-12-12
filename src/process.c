/*
 * process.c - Process Management Implementation
 *
 * Implements process creation, loading, execution, and cleanup.
 * See process.h for API documentation.
 */

#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "memory.h"
#include "context.h"
#include "string.h"
#include <stddef.h>

/* Page size for stack allocation */
#define PROCESS_STACK_SIZE 0x1000

/*
 * Static process storage.
 *
 * For simplicity, we use a single static process struct rather than
 * dynamic allocation. This limits us to one process at a time, but
 * avoids the need for a kernel heap allocator.
 *
 * TODO: Support multiple processes with proper allocation.
 */
static struct process current_process;
static int process_in_use = 0;

static int last_exit_code = 0;

int process_get_exit_code() {
    return last_exit_code;
}   

void process_set_exit_code(int code) {
    last_exit_code = code;
}

struct process *process_create(void) {
    /* Only one process at a time for now */
    if (process_in_use) {
        return NULL;
    }

    struct process *p = &current_process;

    /* Initialize to zero */
    p->pml4 = 0;
    p->code_page = 0;
    p->stack_page = 0;
    p->entry = USER_CODE_BASE;
    p->stack = USER_STACK_BASE + PROCESS_STACK_SIZE;
    p->exit_code = 0;

    /* Create new address space (PML4 with kernel mappings) */
    p->pml4 = vmm_create_address_space();
    if (p->pml4 == 0) {
        return NULL;
    }

    /* Allocate code page */
    p->code_page = pmm_alloc();
    if (p->code_page == 0) {
        /* TODO: Free PML4 */
        return NULL;
    }

    /* Allocate stack page */
    p->stack_page = pmm_alloc();
    if (p->stack_page == 0) {
        pmm_free(p->code_page);
        /* TODO: Free PML4 */
        return NULL;
    }

    /* Map code page at USER_CODE_BASE */
    if (vmm_map_page(p->pml4, USER_CODE_BASE, p->code_page,
                     PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
        pmm_free(p->stack_page);
        pmm_free(p->code_page);
        /* TODO: Free PML4 */
        return NULL;
    }

    /* Map stack page at USER_STACK_BASE */
    if (vmm_map_page(p->pml4, USER_STACK_BASE, p->stack_page,
                     PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
        pmm_free(p->stack_page);
        pmm_free(p->code_page);
        /* TODO: Free PML4 and unmap code page */
        return NULL;
    }

    process_in_use = 1;
    return p;
}

int process_load(struct process *p, const void *code, uint32_t len) {
    if (p == NULL || code == NULL) {
        return -1;
    }

    /* Ensure code fits in one page */
    if (len > PROCESS_STACK_SIZE) {
        return -1;
    }

    /* Copy code to the code page via HHDM */
    void *code_virt = phys_to_virt(p->code_page);
    memcpy(code_virt, code, len);

    return 0;
}

int process_run(struct process *p) {
    if (p == NULL) {
        return -1;
    }

    /* Set up user context for context switch */
    struct user_context ctx;
    ctx.pml4 = p->pml4;
    ctx.entry = p->entry;
    ctx.stack = p->stack;

    /* Enter userspace - blocks until sys_exit */
    context_switch_to_user(&ctx);

    /* Process has exited, return exit code */
    /* TODO: Capture actual exit code from sys_exit */
    return last_exit_code;
}

void process_destroy(struct process *p) {
    if (p == NULL) {
        return;
    }

    /* Free allocated pages */
    if (p->stack_page != 0) {
        pmm_free(p->stack_page);
    }
    if (p->code_page != 0) {
        pmm_free(p->code_page);
    }
    if (p->pml4 != 0) {
        pmm_free(p->pml4);
    }

    /* TODO: Free PML4 and intermediate page tables */
    /* For now we leak the PML4 - proper cleanup requires walking
     * the page table hierarchy and freeing all user-space tables */

    /* Mark as available */
    process_in_use = 0;
}
