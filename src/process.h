/*
 * process.h - Process Management
 *
 * This module encapsulates the creation and execution of user processes.
 * It handles address space setup, binary loading, and context switching,
 * providing a clean abstraction over the low-level memory and context APIs.
 *
 * Process Lifecycle:
 *
 *   1. process_create()  - Allocate address space and memory pages
 *   2. process_load()    - Copy program binary into address space
 *   3. process_run()     - Execute until sys_exit (blocks)
 *   4. process_destroy() - Free all allocated resources
 *
 * Example Usage:
 *
 *   struct process *p = process_create();
 *   if (p) {
 *       process_load(p, binary_code, binary_len);
 *       int exit_code = process_run(p);
 *       process_destroy(p);
 *   }
 *
 * Memory Layout (per process):
 *
 *   Each process gets its own address space (PML4) with:
 *   - Code page mapped at USER_CODE_BASE (0x400000)
 *   - Stack page mapped at USER_STACK_BASE (0x7FFFFF000)
 *   - Kernel mapped in upper half (shared across all processes)
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "vfs.h"
#include "types.h"

/*
 * struct process - Represents a user process.
 *
 * Contains all resources allocated for a process. These are tracked
 * so they can be properly freed when the process terminates.
 */
struct process {
    uint64_t pml4;          /* Physical address of PML4 (address space root) */
    uint64_t code_page;     /* Physical address of code page */
    uint64_t stack_page;    /* Physical address of stack page */
    uint64_t entry;         /* Entry point virtual address */
    uint64_t brk;           /* Current break address for sbrk() */
    uint64_t stack;         /* Initial stack pointer */
    int exit_code;          /* Exit code after process terminates */
    int pid;                /* Process ID */
    struct fd_table fds;    /* File descriptor table */
};

void *process_sbrk(int64_t increment);

int process_get_pid(void);

int process_get_exit_code();

void process_set_exit_code(int code);

/*
 * process_create - Create a new process with its own address space.
 *
 * Allocates:
 *   - A new PML4 (page tables) with kernel mappings
 *   - A physical page for code
 *   - A physical page for stack
 *
 * Maps the code and stack pages into the new address space.
 *
 * Returns: Pointer to process struct, or NULL on failure.
 *
 * Note: The returned process must be freed with process_destroy().
 */
struct process *process_create(void);

/*
 * process_load - Load raw binary code into the process address space.
 *
 * @p:    Process to load into (must have been created with process_create)
 * @code: Pointer to the binary code
 * @len:  Length of the binary code in bytes
 *
 * Copies the binary code to the process's code page. The code will
 * be visible at USER_CODE_BASE when the process runs.
 *
 * Returns: 0 on success, -1 on failure.
 *
 * Note: Prefer process_load_elf() for loading ELF executables.
 */
int process_load(struct process *p, const void *code, uint32_t len);

/*
 * process_load_elf - Load an ELF executable into the process address space.
 *
 * @p:    Process to load into (must have been created with process_create)
 * @data: Pointer to the ELF file data
 * @size: Size of the ELF data in bytes
 *
 * Parses the ELF header and loads all PT_LOAD segments at their
 * specified virtual addresses. Sets the process entry point from
 * the ELF header.
 *
 * Returns: 0 on success, -1 on failure.
 */
int process_load_elf(struct process *p, const void *data, uint64_t size);

/*
 * process_run - Execute the process until it exits.
 *
 * @p: Process to execute (must have been loaded with process_load)
 *
 * This function blocks until the process calls sys_exit(). It:
 *   1. Sets up the user context (PML4, entry point, stack)
 *   2. Switches to the process's address space
 *   3. Enters ring 3 via IRETQ
 *   4. Returns when sys_exit() is called
 *
 * Returns: The process's exit code.
 */
int process_run(struct process *p);

/*
 * process_run_with_args - Execute the process with command-line arguments.
 *
 * @p:    Process to execute
 * @argc: Number of arguments
 * @argv: Array of argument strings (NULL-terminated)
 *
 * Sets up argc/argv on the user stack before entering userspace.
 * The C runtime (crt0) will pop these and pass them to main().
 *
 * Stack layout on entry:
 *   [strings...]    <- high addresses
 *   [NULL]
 *   [argv[n-1] ptr]
 *   ...
 *   [argv[0] ptr]
 *   [argc]          <- RSP points here
 *
 * Returns: The process's exit code.
 */
int process_run_with_args(struct process *p, int argc, char **argv);

/*
 * process_destroy - Free all resources associated with a process.
 *
 * @p: Process to destroy (may be NULL, in which case this is a no-op)
 *
 * Frees:
 *   - The code page
 *   - The stack page
 *   - The PML4 (but not intermediate page tables - TODO)
 *   - The process struct itself
 */
void process_destroy(struct process *p);

/**
 * @brief Get the file descriptor table of the current process.
 * 
 * Retrieves a pointer to the file descriptor table associated with the 
 * currently executing process. The file descriptor table maintains the 
 * mapping between file descriptor integers and their corresponding file 
 * objects or resources.
 * 
 * @return Pointer to the file descriptor table of the current process.
 *         Returns NULL if no process is currently active or if the 
 *         current process has no file descriptor table allocated.
 * 
 * @note This function should only be called in process context.
 * @note The returned pointer should not be freed by the caller.
 */
struct fd_table *process_get_fd_table(void);

#endif /* PROCESS_H */
