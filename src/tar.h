#ifndef TAR_H
#define TAR_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TAR_BLOCK_SIZE 512
#define TAR_TYPE_FILE     '0'
#define TAR_TYPE_DIR      '5'


struct tar_header {
    char name[100];      // Filename (e.g., "bin/hello")
    char mode[8];        // File mode (octal ASCII)
    char uid[8];         // Owner UID (octal)
    char gid[8];         // Owner GID (octal)
    char size[12];       // File size (octal ASCII!)
    char mtime[12];      // Modification time
    char checksum[8];    // Header checksum
    char typeflag;       // '0'=file, '5'=directory
    char linkname[100];  // Link target
    char magic[6];       // "ustar\0"
    char version[2];     // "00"
    char uname[32];      // Owner username
    char gname[32];      // Owner group
    char devmajor[8];    // Device major
    char devminor[8];    // Device minor
    char prefix[155];    // Filename prefix (for long names)
    char padding[12];    // Pad to 512 bytes
};

struct tar_file {
    char name[100];
    const uint8_t *data;
    size_t size;
    uint8_t is_dir;
};


void tar_init(const uint8_t *archive, size_t archive_size);

struct tar_file *tar_find(const char *path);

int tar_list_dir(const char *dir,
                 void (*callback)(const char *name, size_t size, int is_dir, void *ctx),
                 void *ctx);

size_t tar_get_file_count(void);
 
#endif /* TAR_H */