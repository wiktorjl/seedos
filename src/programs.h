/*
 * programs.h - Program Loader Interface
 *
 * Provides the interface for loading and executing user programs
 * from the TAR-based initrd filesystem.
 */

#ifndef PROGRAMS_H
#define PROGRAMS_H

/*
 * programs_run - Load and execute a program from the initrd.
 *
 * @name: Program name or path (e.g., "hello" or "bin/hello")
 * @argc: Argument count to pass to program
 * @argv: Argument vector to pass to program
 *
 * If name doesn't start with "bin/", it's automatically prepended.
 *
 * Returns: Program exit code, or -1 if program not found or failed to load.
 */
int programs_run(const char *name, int argc, char **argv);

#endif /* PROGRAMS_H */
