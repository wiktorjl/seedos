/*
 * ls.c - List directory contents
 *
 * Implementation of the ls command with Linux-like output.
 *
 * Usage:
 *   ls           - List current directory
 *   ls /bin      - List /bin directory
 *   ls -l        - Long format (permissions, size, name)
 *   ls -a        - Show all files (including hidden)
 *   ls -la       - Long format with all files
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
    int show_all = 0;

    /* Parse arguments */
    for(int i = 1; i < argc; i++) {
        if(argv[i][0] == '-') {
            /* Parse flags */
            for(int j = 1; argv[i][j]; j++) {
                switch(argv[i][j]) {
                    case 'l': long_format = 1; break;
                    case 'a': show_all = 1; break;
                }
            }
        }else {
            path = argv[i];
            display_path = argv[i];
        }
    }

    /* Let the kernel handle path resolution - just pass it through */
    DIR *dir = opendir(path);
    if(dir == NULL) {
        printf("ls: cannot access '%s': No such file or directory\n", display_path);
        return 1;
    }

    /* Get the real path for stat calls */
    char cwd[256];
    char full_path[512];
    if(strcmp(path, ".") == 0) {
        if(getcwd(cwd, sizeof(cwd)) == NULL) {
            strcpy(cwd, "/");
        }
        path = cwd;
    }

    /* Count total blocks for long format header */
    unsigned long total_blocks = 0;
    struct dirent *entry;

    if(long_format) {
        while((entry = readdir(dir)) != NULL) {
            /* Skip hidden files unless -a */
            if(!show_all && entry->d_name[0] == '.') continue;

            /* Build full path for stat */
            if(strcmp(path, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
            }else {
                snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            }

            struct stat st;
            if(stat(full_path, &st) == 0) {
                total_blocks += (st.st_size + 511) / 512;
            }
        }
        printf("total %lu\n", total_blocks);

        /* Rewind directory */
        closedir(dir);
        dir = opendir(path);
        if(dir == NULL) return 1;
    }

    while((entry = readdir(dir)) != NULL) {
        /* Skip hidden files unless -a */
        if(!show_all && entry->d_name[0] == '.') continue;

        int is_dir = (entry->d_type == DT_DIR);

        if(long_format) {
            /* Build full path for stat */
            if(strcmp(path, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
            }else {
                snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            }

            struct stat st;
            unsigned long size = 0;
            if(stat(full_path, &st) == 0) {
                size = st.st_size;
            }

            /* Print permissions string */
            if(is_dir) {
                printf("drwxr-xr-x");
            }else {
                /* Check if executable (files in /bin) */
                int is_exec = 0;
                if(strncmp(path, "/bin", 4) == 0 || strcmp(path, "bin") == 0) {
                    is_exec = 1;
                }
                if(is_exec) {
                    printf("-rwxr-xr-x");
                }else {
                    printf("-rw-r--r--");
                }
            }

            /* Links, owner, group */
            printf(" %2d %-5s %-5s", is_dir ? 2 : 1, "root", "root");

            /* Size - right aligned in 8 chars */
            printf(" %8lu", size);

            /* Date - we don't have real timestamps, use placeholder */
            printf(" Jan  1 00:00");

            /* Filename with color hint for directories */
            if(is_dir) {
                printf(" %s/\n", entry->d_name);
            }else {
                printf(" %s\n", entry->d_name);
            }
        }else {
            /* Short format: name with / suffix for directories */
            if(is_dir) {
                printf("%s/\n", entry->d_name);
            }else {
                printf("%s\n", entry->d_name);
            }
        }
    }

    closedir(dir);
    return 0;
}
