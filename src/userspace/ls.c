/*
 * ls.c - List directory contents
 *
 * Simple implementation of the ls command.
 * Lists files in the current directory or specified path.
 *
 * Usage:
 *   ls           - List current directory
 *   ls /bin      - List /bin directory
 *   ls -l        - Long format (not yet implemented)
 */

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = ".";
    int long_format = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            long_format = 1;
        } else {
            path = argv[i];
        }
    }

    /* If path is ".", use current working directory */
    char cwd[256];
    if (strcmp(path, ".") == 0) {
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            /* For root, we need to list with empty prefix */
            if (strcmp(cwd, "/") == 0) {
                path = "";
            } else {
                /* Skip leading slash for tarfs */
                path = cwd + 1;
            }
        }
    } else if (path[0] == '/') {
        /* Skip leading slash for tarfs */
        path = path + 1;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        printf("ls: cannot access '%s': No such file or directory\n",
               argc > 1 ? argv[1] : ".");
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
