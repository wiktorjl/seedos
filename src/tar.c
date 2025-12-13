#include "tar.h"
#include <string.h>

#define MAX_TAR_FILES 64
static struct tar_file tar_files[MAX_TAR_FILES];
static int tar_file_count = 0;

// Parse octal ASCII size field
static size_t tar_parse_octal(const char *s, int len) {
    size_t value = 0;
    for (int i = 0; i < len && s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '7') {
            value = (value << 3) | (s[i] - '0');
        }
    }
    return value;
}


void tar_init(const uint8_t *archive, size_t archive_size) {
    const uint8_t *ptr = archive;
    const uint8_t *end = archive + archive_size;

    tar_file_count = 0;

    while (ptr + TAR_BLOCK_SIZE <= end) {
        struct tar_header *hdr = (struct tar_header *)ptr;

        // End of archive (two zero blocks)
        if (hdr->name[0] == '\0') break;

        size_t size = tar_parse_octal(hdr->size, 12);

        // Store file entry
        if (tar_file_count < MAX_TAR_FILES) {
            strncpy(tar_files[tar_file_count].name, hdr->name, 99);
            tar_files[tar_file_count].name[99] = '\0';
            tar_files[tar_file_count].data = ptr + TAR_BLOCK_SIZE;
            tar_files[tar_file_count].size = size;
            tar_files[tar_file_count].is_dir = (hdr->typeflag == '5');
            tar_file_count++;
        }

        // Advance to next header (header + data rounded to 512)
        size_t blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        ptr += TAR_BLOCK_SIZE + (blocks * TAR_BLOCK_SIZE);
    }
}

// Find file by path
struct tar_file *tar_find(const char *path) {
    // Skip leading slash
    if (path[0] == '/') path++;

    for (int i = 0; i < tar_file_count; i++) {
        if (strcmp(tar_files[i].name, path) == 0) {
            return &tar_files[i];
        }
        // Also try without trailing slash for directories
        size_t len = strlen(tar_files[i].name);
        if (len > 0 && tar_files[i].name[len-1] == '/') {
            if (strncmp(tar_files[i].name, path, len-1) == 0 &&
                path[len-1] == '\0') {
                return &tar_files[i];
            }
        }
    }
    return NULL;
}

// List files in directory (for shell 'ls' command)
int tar_list_dir(const char *dir,
                 void (*callback)(const char *name, size_t size, int is_dir, void *ctx),
                 void *ctx) {
    size_t dir_len = strlen(dir);
    int count = 0;

    for (int i = 0; i < tar_file_count; i++) {
        const char *name = tar_files[i].name;

        // Check if file is in this directory
        if (dir_len == 0 || strncmp(name, dir, dir_len) == 0) {
            const char *rest = name + dir_len;
            if (dir_len > 0 && *rest == '/') rest++;

            // Only direct children (no nested slashes except trailing)
            char *slash = strchr(rest, '/');
            if (!slash || (slash[1] == '\0')) {
                callback(rest, tar_files[i].size, tar_files[i].is_dir, ctx);
                count++;
            }
        }
    }
    return count;
}

size_t tar_get_file_count(void) {
    return tar_file_count;
}