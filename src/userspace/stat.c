/*
 * stat.c - Display file status
 *
 * Usage: stat FILE [FILE ...]
 */

#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: stat FILE [FILE ...]\n");
        return 1;
    }

    int ret = 0;

    for (int i = 1; i < argc; i++) {
        struct stat st;

        if (stat(argv[i], &st) != 0) {
            fprintf(stderr, "stat: cannot stat '%s': No such file\n", argv[i]);
            ret = 1;
            continue;
        }

        printf("  File: %s\n", argv[i]);
        printf("  Size: %lu\n", (unsigned long)st.st_size);

        /* Determine file type */
        const char *type;
        if (S_ISDIR(st.st_mode)) {
            type = "directory";
        } else if (S_ISREG(st.st_mode)) {
            type = "regular file";
        } else {
            type = "unknown";
        }
        printf("  Type: %s\n", type);

        if (i < argc - 1) {
            putchar('\n');
        }
    }

    return ret;
}
