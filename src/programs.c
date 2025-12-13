/*
 * programs.c - Program Loader
 *
 * Loads and executes user programs from the TAR-based initrd filesystem.
 * Programs are stored as ELF executables in /bin/ within the initrd.
 *
 * Usage:
 *   int exit_code = programs_run("hello", argc, argv);
 *
 * This will look for "bin/hello" in the initrd, load the ELF, and execute it.
 */

#include <stddef.h>
#include "programs.h"
#include "process.h"
#include "string.h"
#include "console.h"
#include "tar.h"

/*
 * programs_run - Load and execute a program from the initrd.
 *
 * @name: Program name or path (e.g., "hello" or "bin/hello")
 * @argc: Argument count to pass to program
 * @argv: Argument vector to pass to program
 *
 * If name doesn't start with "bin/", it's automatically prepended.
 *
 * Returns: Program exit code, or -1 on error.
 */
int programs_run(const char *name, int argc, char **argv) {
    char path[128];
    size_t i = 0;

    /* Only prepend "bin/" if path doesn't already start with it */
    if (!(name[0] == 'b' && name[1] == 'i' && name[2] == 'n' && name[3] == '/')) {
        path[0] = 'b'; path[1] = 'i'; path[2] = 'n'; path[3] = '/';
        i = 4;
    }

    /* Copy the name */
    const char *p = name;
    while (*p && i < sizeof(path) - 1) {
        path[i++] = *p++;
    }
    path[i] = '\0';

    /* Find program in initrd */
    const struct tar_file *file = tar_find(path);
    if (file == NULL) {
        return -1;
    }

    /* Create new process */
    struct process *proc = process_create();
    if (proc == NULL) {
        puts("Error: Failed to create process\n");
        return -1;
    }

    /* Load ELF executable into process address space */
    if (process_load_elf(proc, file->data, file->size) != 0) {
        puts("Error: Failed to load ELF\n");
        process_destroy(proc);
        return -1;
    }

    /* Run with argc/argv on stack */
    int exit_code = process_run_with_args(proc, argc, argv);
    process_destroy(proc);
    return exit_code;
}
