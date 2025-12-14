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
 * programs_run_cwd - Load and execute a program with a specific initial cwd.
 *
 * @name: Program name or path (e.g., "hello" or "data/myprog")
 * @argc: Argument count to pass to program
 * @argv: Argument vector to pass to program
 * @cwd:  Initial working directory (e.g., "/" or "/bin")
 *
 * If name contains '/', it's treated as a full path.
 * Otherwise, "bin/" is prepended (e.g., "hello" -> "bin/hello").
 *
 * Returns: Program exit code, or -1 on error.
 */
int programs_run_cwd(const char *name, int argc, char **argv, const char *cwd) {
    char path[128];
    size_t i = 0;

    /* Check if name contains a slash (i.e., is a path) */
    int has_slash = 0;
    for (const char *c = name; *c; c++) {
        if (*c == '/') {
            has_slash = 1;
            break;
        }
    }

    /* Only prepend "bin/" if name is not already a path */
    if (!has_slash) {
        strcpy(path, "bin/"); 
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

    /* Set initial working directory */
    if (cwd != NULL && cwd[0] != '\0') {
        process_set_cwd(cwd);
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

/*
 * programs_run - Load and execute a program from the initrd.
 *
 * Wrapper that calls programs_run_cwd with "/" as the initial cwd.
 */
int programs_run(const char *name, int argc, char **argv) {
    return programs_run_cwd(name, argc, argv, "/");
}
