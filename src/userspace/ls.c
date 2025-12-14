/*
 * ls.c - List directory contents
 *
 * Simple implementation of the ls command.
 * Lists files in the current directory or specified path.
 *
 * Usage:
 *   ls           - List current directory
 *   ls /bin      - List /bin directory
 *   ls -l        - Long format
 */

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = ".";
    const char *display_path = ".";
    int long_format = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            long_format = 1;
        } else {
            path = argv[i];
            display_path = argv[i];
        }
    }

    /* Let the kernel handle path resolution - just pass it through */
    DIR *dir = opendir(path);
    if (dir == NULL) {
        printf("ls: cannot access '%s': No such file or directory\n", display_path);
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (long_format) {
            /* Long format: type + name */
            char type = (entry->d_type == DT_DIR) ? 'd' : '-';
            printf("%c  %s\n", type, entry->d_name);
        } else {
            /* Short format: just name */
            printf("%s\n", entry->d_name);
        }
    }

    closedir(dir);
    return 0;
}
