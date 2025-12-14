/*
 * init.c - SeedOS Init Process (PID 1)
 *
 * The first userspace process started by the kernel.
 * Its job is to start the shell and restart it if it exits.
 *
 * In a real OS, init would:
 *   - Mount filesystems
 *   - Start system services
 *   - Handle orphaned processes
 *   - Shutdown/reboot handling
 *
 * For SeedOS, we just loop forever starting the shell.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("init: SeedOS init process started (PID %d)\n", getpid());

    /* Main init loop - keep restarting the shell */
    while(1) {
        printf("init: Starting shell...\n");

        /*
         * Run the shell using blocking spawn().
         * This waits for shell to exit before returning.
         *
         * Note: True async multitasking requires more complex
         * kernel support for blocking syscalls. For now, processes
         * run sequentially (shell runs, then returns here).
         */
        int exit_code = spawn("/bin/sh", NULL);

        printf("init: Shell exited with code %d\n", exit_code);
        printf("init: Restarting...\n\n");
    }

    return 0;
}
