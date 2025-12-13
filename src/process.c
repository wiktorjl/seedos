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
#include "elf.h"
#include "console.h"
#include <stddef.h>
#include <stdint.h>

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

static int next_pid = 1;

static int process_in_use = 0;

static int last_exit_code = 0;

int process_get_exit_code() {
    return last_exit_code;
}   

void process_set_exit_code(int code) {
    last_exit_code = code;
}

int process_get_pid(void) {
    if (!process_in_use) {
        return -1;
    }
    return current_process.pid;
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
    p->pid = next_pid++; 
    p->brk = USER_HEAP_BASE;

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

    /* Initialize file descriptor table */
    struct fd_table empty_fdt = {0};
    p->fds = empty_fdt;

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

int process_load_elf(struct process *p, const void *data, uint64_t size) {
    if (p == NULL || data == NULL) {
        return -1;
    }

    uint64_t entry;

    /* Use ELF loader to load segments into process address space */
    if (elf_load(data, size, p->pml4, &entry) != 0) {
        return -1;
    }

    /* Set entry point from ELF header */
    p->entry = entry;

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

int process_run_with_args(struct process *p, int argc, char **argv) {
    if (p == NULL) {
        return -1;
    }

    /*
     * Set up argc/argv on the user stack.
     *
     * Stack layout (high to low):
     *   [argv strings]    <- at top of stack page
     *   [NULL]            <- argv terminator
     *   [argv[n-1] ptr]
     *   ...
     *   [argv[0] ptr]
     *   [argc]            <- RSP points here
     */

    /* Get kernel-accessible pointer to stack page */
    uint8_t *stack_virt = (uint8_t *)phys_to_virt(p->stack_page);
    uint8_t *stack_top = stack_virt + PROCESS_STACK_SIZE;

    /* Step 1: Copy strings to top of stack (high addresses, working down) */
    uint64_t string_user_addrs[32];  /* Max 32 arguments */
    if (argc > 32) {
        argc = 32;
    }

    uint8_t *str_ptr = stack_top;
    for (int i = argc - 1; i >= 0; i--) {
        /* Calculate string length including null terminator */
        size_t len = 0;
        const char *s = argv[i];
        while (s[len]) len++;
        len++;  /* Include null terminator */

        /* Move pointer down and copy string */
        str_ptr -= len;
        memcpy(str_ptr, argv[i], len);

        /* Calculate user-space address of this string */
        uint64_t offset = str_ptr - stack_virt;
        string_user_addrs[i] = USER_STACK_BASE + offset;
    }

    /* Step 2: Align to 8 bytes for pointer array */
    str_ptr = (uint8_t *)((uint64_t)str_ptr & ~7ULL);

    /* Step 3: Push NULL terminator for argv */
    str_ptr -= 8;
    *(uint64_t *)str_ptr = 0;

    /* Step 4: Push argv pointers (in reverse order so argv[0] is at lowest address) */
    for (int i = argc - 1; i >= 0; i--) {
        str_ptr -= 8;
        *(uint64_t *)str_ptr = string_user_addrs[i];
    }

    /* Step 5: Push argc */
    str_ptr -= 8;
    *(uint64_t *)str_ptr = (uint64_t)argc;

    /* Calculate new user RSP */
    uint64_t new_stack = USER_STACK_BASE + (str_ptr - stack_virt);

    /* Set up user context for context switch */
    struct user_context ctx;
    ctx.pml4 = p->pml4;
    ctx.entry = p->entry;
    ctx.stack = new_stack;

    /* Enter userspace - blocks until sys_exit */
    context_switch_to_user(&ctx);

    return last_exit_code;
}

void process_destroy(struct process *p) {
    if (p == NULL) {
        return;
    }

    if (p->pml4 != 0) {
        // pmm_free(p->pml4);
        vmm_free_user_address_space(p->pml4);
        p->pml4 = 0;
        p->code_page = 0;
        p->stack_page = 0;
    }

    /* Mark as available */
    process_in_use = 0;
}


void * process_sbrk(intptr_t increment) {
    if (!process_in_use) {
        return (void *)-1;
    }

    struct process *p = &current_process;
    uint64_t old_brk = p->brk;
    uint64_t new_brk = old_brk + increment;

    /* Don't allow shrinking below heap base */
    if (new_brk < USER_HEAP_BASE) {
        return (void *)-1;
    }

    uint64_t old_page = (old_brk - 1) / VMM_PAGE_SIZE;
    uint64_t new_page = (new_brk - 1) / VMM_PAGE_SIZE;

    /* Allocate new pages if growing */
    if (new_brk > old_brk) {
        for (uint64_t page = old_page + 1; page <= new_page; page++) {
            uint64_t virt = page * VMM_PAGE_SIZE;
            uint64_t phys = pmm_alloc();
            if (phys == 0) {
                return (void *)-1;
            }
            if (vmm_map_page(p->pml4, virt, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
                pmm_free(phys);
                return (void *)-1;
            }
            void *page_virt = phys_to_virt(phys);
            memset(page_virt, 0, VMM_PAGE_SIZE);
        }
    } else if (new_brk < old_brk) {
        for (uint64_t page = new_page + 1; page <= old_page; page++) {
            uint64_t virt = page * VMM_PAGE_SIZE;
            uint64_t phys = vmm_get_physical(p->pml4, virt);
            if(phys != 0) {
                vmm_unmap_page(p->pml4, virt);
                pmm_free(phys);
            }
        }
    }

    p->brk = new_brk;
    return (void *)old_brk;
}

struct fd_table *process_get_fd_table(void) {
    if (!process_in_use) {
        return NULL;
    }
    return &current_process.fds;
}