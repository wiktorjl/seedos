/*
 * tar.c - TAR Archive Parser
 *
 * Parses USTAR format TAR archives used for the initrd filesystem.
 * Provides functions to initialize the archive, find files by path,
 * and list directory contents.
 */

#include "tar.h"
#include <string.h>

#define MAX_TAR_FILES 64

static struct tar_file tar_files[MAX_TAR_FILES];
static int tar_file_count = 0;

/*
 * tar_parse_octal - Parse an octal ASCII string to integer.
 *
 * TAR headers store sizes as octal ASCII strings.
 */
static size_t tar_parse_octal(const char *s, int len) {
    size_t value = 0;
    for (int i = 0; i < len && s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '7') {
            value = (value << 3) | (s[i] - '0');
        }
    }
    return value;
}

/*
 * tar_init - Initialize the TAR parser with an archive.
 *
 * @archive:      Pointer to the TAR archive data
 * @archive_size: Size of the archive in bytes
 *
 * Parses the archive and builds an internal file index.
 * Strips "./" prefix from paths for cleaner lookups.
 */
void tar_init(const uint8_t *archive, size_t archive_size) {
    const uint8_t *ptr = archive;
    const uint8_t *end = archive + archive_size;

    tar_file_count = 0;

    while (ptr + TAR_BLOCK_SIZE <= end) {
        struct tar_header *hdr = (struct tar_header *)ptr;

        /* End of archive (two zero blocks) */
        if (hdr->name[0] == '\0') break;

        size_t size = tar_parse_octal(hdr->size, 12);

        /* Store file entry */
        if (tar_file_count < MAX_TAR_FILES) {
            /* Strip "./" prefix if present */
            const char *name = hdr->name;
            if (name[0] == '.' && name[1] == '/') {
                name += 2;
            }

            /* Skip empty names (the "./" root entry becomes empty after stripping) */
            if (name[0] == '\0') {
                size_t blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
                ptr += TAR_BLOCK_SIZE + (blocks * TAR_BLOCK_SIZE);
                continue;
            }

            strncpy(tar_files[tar_file_count].name, name, 99);
            tar_files[tar_file_count].name[99] = '\0';
            tar_files[tar_file_count].data = ptr + TAR_BLOCK_SIZE;
            tar_files[tar_file_count].size = size;
            tar_files[tar_file_count].is_dir = (hdr->typeflag == '5');
            tar_file_count++;
        }

        /* Advance to next header (header + data rounded to 512) */
        size_t blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        ptr += TAR_BLOCK_SIZE + (blocks * TAR_BLOCK_SIZE);
    }
}

/*
 * tar_find - Find a file by path in the archive.
 *
 * @path: Path to the file (e.g., "bin/hello")
 *
 * Returns: Pointer to tar_file structure, or NULL if not found.
 */
struct tar_file *tar_find(const char *path) {
    /* Skip leading slash */
    if (path[0] == '/') path++;

    for (int i = 0; i < tar_file_count; i++) {
        if (strcmp(tar_files[i].name, path) == 0) {
            return &tar_files[i];
        }
        /* Also try without trailing slash for directories */
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

/*
 * tar_list_dir - List files in a directory.
 *
 * @dir:      Directory path (empty string for root)
 * @callback: Function called for each entry
 * @ctx:      User context passed to callback
 *
 * Returns: Number of entries found.
 */
int tar_list_dir(const char *dir,
                 void (*callback)(const char *name, size_t size, int is_dir, void *ctx),
                 void *ctx) {
    size_t dir_len = strlen(dir);
    int count = 0;

    for (int i = 0; i < tar_file_count; i++) {
        const char *name = tar_files[i].name;

        /* Check if file is in this directory */
        if (dir_len == 0 || strncmp(name, dir, dir_len) == 0) {
            const char *rest = name + dir_len;
            if (dir_len > 0 && *rest == '/') rest++;

            /* Only direct children (no nested slashes except trailing) */
            const char *slash = strchr(rest, '/');
            if (!slash || (slash[1] == '\0')) {
                callback(rest, tar_files[i].size, tar_files[i].is_dir, ctx);
                count++;
            }
        }
    }
    return count;
}

/*
 * tar_get_file_count - Get the number of files in the archive.
 */
size_t tar_get_file_count(void) {
    return tar_file_count;
}
