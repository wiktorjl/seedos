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
#include <stddef.h>
#include <stdint.h>
#include "sched.h"

/* Note: PROCESS_STACK_SIZE and PROCESS_STACK_PAGES are defined in process.h */

/*
 * Static process storage.
 *
 * We support two process slots: one for the main process and one for
 * spawned child processes. This allows a shell to run programs.
 */
 static struct process process_slots[MAX_PROCESSES];

static struct process *current_process = NULL;

static int next_pid = 1;

static int last_exit_code = 0;

int process_get_exit_code() {
    return last_exit_code;
}   

void process_set_exit_code(int code) {
    last_exit_code = code;
}

int process_get_pid(void) {
    if(current_process == NULL) {
        return -1;
    }
    return current_process->pid;
}

struct process *process_create(void) {
    /* Find a free slot */
    int slot = -1;
     for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_slots[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if(slot < 0) {
        return NULL;  /* No free slots */
    }

    struct process *p = &process_slots[slot];

    /* Initialize to zero */
    p->pml4 = 0;
    p->code_page = 0;
    p->stack_page_count = 0;
    for(int i = 0; i < PROCESS_STACK_PAGES; i++) {
        p->stack_pages[i] = 0;
    }
    p->entry = USER_CODE_BASE;
    p->stack = USER_STACK_TOP;  /* RSP starts at top of stack */
    p->exit_code = 0;
    p->wait_pid = 0;
    p->pid = next_pid++;
    p->brk = USER_HEAP_BASE;

    /* Create new address space (PML4 with kernel mappings) */
    p->pml4 = vmm_create_address_space();
    if(p->pml4 == 0) {
        return NULL;
    }

    /* Allocate code page */
    p->code_page = pmm_alloc();
    if(p->code_page == 0) {
        vmm_free_user_address_space(p->pml4);
        return NULL;
    }

    /* Allocate stack pages (64KB = 16 pages) */
    for(int i = 0; i < PROCESS_STACK_PAGES; i++) {
        p->stack_pages[i] = pmm_alloc();
        if(p->stack_pages[i] == 0) {
            /* Free already allocated pages */
            for(int j = 0; j < i; j++) {
                pmm_free(p->stack_pages[j]);
            }
            pmm_free(p->code_page);
            vmm_free_user_address_space(p->pml4);
            return NULL;
        }
        p->stack_page_count++;
    }

    /* Map code page at USER_CODE_BASE */
    if(vmm_map_page(p->pml4, USER_CODE_BASE, p->code_page,
                     PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
        for(int i = 0; i < p->stack_page_count; i++) {
            pmm_free(p->stack_pages[i]);
        }
        pmm_free(p->code_page);
        vmm_free_user_address_space(p->pml4);
        return NULL;
    }

    /* Map all stack pages contiguously from USER_STACK_BASE */
    for(int i = 0; i < PROCESS_STACK_PAGES; i++) {
        uint64_t vaddr = USER_STACK_BASE + (i * 0x1000);
        if(vmm_map_page(p->pml4, vaddr, p->stack_pages[i],
                         PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
            for(int j = 0; j < p->stack_page_count; j++) {
                pmm_free(p->stack_pages[j]);
            }
            pmm_free(p->code_page);
            vmm_free_user_address_space(p->pml4);
            return NULL;
        }
    }

    /* Initialize file descriptor table */
    struct fd_table empty_fdt = {0};
    p->fds = empty_fdt;

    /* Initialize current working directory to root */
    p->cwd[0] = '/';
    p->cwd[1] = '\0';
    
    /* Mark process as ready (not UNUSED so slot isn't reused) */
    p->state = PROC_READY;

    /* Don't change current_process here - only when running */
    return p;
}

int process_load_elf(struct process *p, const void *data, uint64_t size) {
    if(p == NULL || data == NULL) {
        return -1;
    }

    uint64_t entry;

    /* Use ELF loader to load segments into process address space */
    if(elf_load(data, size, p->pml4, &entry) != 0) {
        return -1;
    }

    /* Set entry point from ELF header */
    p->entry = entry;

    return 0;
}

int process_run(struct process *p) {
    return process_run_with_args(p, 0, NULL);
}

/*
 * process_setup_argv - Set up argc/argv on the user stack.
 *
 * @p:    Process whose stack to set up
 * @argc: Argument count (must be <= EXEC_MAX_ARGS)
 * @argv: Argument vector (kernel pointers)
 *
 * Returns: User-space stack pointer, or 0 on error (too many args).
 *
 * Stack layout per x86-64 ABI:
 *
 *   High addresses (stack top)
 *   ┌─────────────────────┐
 *   │ "arg2\0"            │  <- argv strings
 *   │ "arg1\0"            │
 *   │ "prog\0"            │
 *   ├─────────────────────┤
 *   │ NULL                │  <- argv terminator
 *   │ ptr to "arg2"       │  <- argv[2]
 *   │ ptr to "arg1"       │  <- argv[1]
 *   │ ptr to "prog"       │  <- argv[0]
 *   ├─────────────────────┤
 *   │ argc (3)            │  <- RSP points here
 *   └─────────────────────┘
 *   Low addresses
 */
uint64_t process_setup_argv(struct process *p, int argc, char **argv) {
    /* Get kernel-accessible pointer to top stack page */
    uint8_t *top_page_virt = (uint8_t *)phys_to_virt(p->stack_pages[PROCESS_STACK_PAGES - 1]);
    uint8_t *stack_top = top_page_virt + 0x1000;

    /* Reject too many arguments */
    if(argc > EXEC_MAX_ARGS) {
        return 0;  /* Error: too many arguments */
    }

    /* Step 1: Copy argument strings to top of stack */
    uint64_t string_user_addrs[EXEC_MAX_ARGS];
    uint8_t *str_ptr = stack_top;

    for(int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        str_ptr -= len;
        memcpy(str_ptr, argv[i], len);

        uint64_t offset_in_page = str_ptr - top_page_virt;
        string_user_addrs[i] = (USER_STACK_TOP - 0x1000) + offset_in_page;
    }

    /* Step 2: Align to 8 bytes */
    str_ptr = (uint8_t *)((uint64_t)str_ptr & ~7ULL);

    /* Step 3: Push NULL terminator */
    str_ptr -= 8;
    *(uint64_t *)str_ptr = 0;

    /* Step 4: Push argv pointers */
    for(int i = argc - 1; i >= 0; i--) {
        str_ptr -= 8;
        *(uint64_t *)str_ptr = string_user_addrs[i];
    }

    /* Step 5: Push argc */
    str_ptr -= 8;
    *(uint64_t *)str_ptr = (uint64_t)argc;

    /* Return user-space stack pointer */
    uint64_t offset_in_page = str_ptr - top_page_virt;
    return (USER_STACK_TOP - 0x1000) + offset_in_page;
}

int process_run_with_args(struct process *p, int argc, char **argv) {
    if(p == NULL) {
        return -1;
    }

    /* Set up argc/argv on stack */
    uint64_t new_stack = process_setup_argv(p, argc, argv);
    if(new_stack == 0) {
        return -1;  /* Too many arguments */
    }

    /* Set up user context for context switch */
    struct user_context ctx;
    ctx.pml4 = p->pml4;
    ctx.entry = p->entry;
    ctx.stack = new_stack;

    /* Set this process as current before entering userspace */
    current_process = p;
    p->state = PROC_RUNNING;

    /* Enter userspace - blocks until sys_exit */
    context_switch_to_user(&ctx);

    return last_exit_code;
}

void process_destroy(struct process *p) {
    if(p == NULL) {
        return;
    }

    if(p->pml4 != 0) {
        vmm_free_user_address_space(p->pml4);
        p->pml4 = 0;
        p->code_page = 0;
        /* Clear stack pages */
        for(int i = 0; i < PROCESS_STACK_PAGES; i++) {
            p->stack_pages[i] = 0;
        }
        p->stack_page_count = 0;
    }

    /* Mark slot as available */
    p->state = PROC_UNUSED;

    /* Update current_process if needed */
    if(current_process == p) {
        /* Find another active process, or set to NULL */
        current_process = NULL;
        for(int i = 0; i < MAX_PROCESSES; i++) {
            if(process_slots[i].state != PROC_UNUSED) {
                current_process = &process_slots[i];
                break;
            }
        }
    }
}


void * process_sbrk(intptr_t increment) {
    if(current_process == NULL) {
        return (void *)-1;
    }

    struct process *p = current_process;
    uint64_t old_brk = p->brk;
    uint64_t new_brk = old_brk + increment;

    /* Don't allow shrinking below heap base */
    if(new_brk < USER_HEAP_BASE) {
        return (void *)-1;
    }

    uint64_t old_page = old_brk == USER_HEAP_BASE ? (USER_HEAP_BASE / VMM_PAGE_SIZE) - 1 : (old_brk - 1) / VMM_PAGE_SIZE;
    uint64_t new_page = (new_brk - 1) / VMM_PAGE_SIZE;

    /* Allocate new pages if growing */
    if(new_brk > old_brk) {
        for(uint64_t page = old_page + 1; page <= new_page; page++) {
            uint64_t virt = page * VMM_PAGE_SIZE;
            uint64_t phys = pmm_alloc();
            if(phys == 0) {
                return (void *)-1;
            }
            if(vmm_map_page(p->pml4, virt, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
                pmm_free(phys);
                return (void *)-1;
            }
            void *page_virt = phys_to_virt(phys);
            memset(page_virt, 0, VMM_PAGE_SIZE);
        }
    }else if(new_brk < old_brk) {
        for(uint64_t page = new_page + 1; page <= old_page; page++) {
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
    if(current_process == NULL) {
        return NULL;
    }
    return &current_process->fds;
}

const char *process_get_cwd(void) {
    if(current_process == NULL) {
        return "/";
    }
    return current_process->cwd;
}

int process_set_cwd(const char *path) {
    if(current_process == NULL || path == NULL) {
        return -1;
    }

    /* Copy path to cwd, ensuring null termination */
    size_t len = strlen(path);
    if(len >= sizeof(current_process->cwd)) {
        return -1;  /* Path too long */
    }

    //strcpy(current_process->cwd, path);
    strncpy(current_process->cwd, path, sizeof(current_process->cwd) - 1);
    return 0;
}

int process_set_cwd_for(struct process *p, const char *path) {
    if(p == NULL || path == NULL) {
        return -1;
    }

    /* Copy path to cwd, ensuring null termination */
    size_t len = strlen(path);
    if(len >= sizeof(p->cwd)) {
        return -1;  /* Path too long */
    }

    strcpy(p->cwd, path);
    return 0;
}

/*
 * process_set_current - Set the current running process.
 *
 * Used by spawn syscall to switch between parent and child.
 */
void process_set_current(struct process *p) {
    current_process = p;
}

/*
 * process_get_current - Get the current running process.
 */
struct process *process_get_current(void) {
    return current_process;
}

int process_start(struct process *p) {
    if(p == NULL) return -1;

    /* Initialize saved context for first entry to userspace */
    p->saved_rip = p->entry;
    p->saved_rsp = p->stack;
    p->saved_rflags = 0x202;  /* IF=1, reserved bit=1 */

    /* Clear all other registers */
    p->saved_rax = 0;
    p->saved_rbx = 0;
    p->saved_rcx = 0;
    p->saved_rdx = 0;
    p->saved_rsi = 0;
    p->saved_rdi = 0;
    p->saved_rbp = 0;
    p->saved_r8 = 0;
    p->saved_r9 = 0;
    p->saved_r10 = 0;
    p->saved_r11 = 0;
    p->saved_r12 = 0;
    p->saved_r13 = 0;
    p->saved_r14 = 0;
    p->saved_r15 = 0;

    sched_add(p);
    return 0;
}

/*
 * process_find_by_pid - Find a process by its PID.
 *
 * @pid: Process ID to search for
 *
 * Returns: Pointer to process struct, or NULL if not found.
 */
struct process *process_find_by_pid(int pid) {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_slots[i].state != PROC_UNUSED && process_slots[i].pid == pid) {
            return &process_slots[i];
        }
    }
    return NULL;
}

/*
 * process_find_blocked_on_pid - Find a process blocked waiting for a PID.
 *
 * @pid: PID being waited for
 *
 * Returns: Pointer to blocked process, or NULL if none found.
 */
struct process *process_find_blocked_on_pid(int pid) {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_slots[i].state == PROC_BLOCKED &&
            process_slots[i].wait_pid == pid) {
            return &process_slots[i];
        }
    }
    return NULL;
}
