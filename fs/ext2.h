/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ext2 Filesystem Structures and Interface
 *
 * Minimal read-only ext2 implementation for loading programs from
 * a RAM disk (initrd). No write support, no caching (RAM disk is
 * already in memory).
 *
 * On-disk layout:
 *   Block 0:     Boot block (unused, 1024 bytes)
 *   Block 1:     Superblock (at byte offset 1024)
 *   Block 2+:    Block Group Descriptor Table
 *   ...          Block/inode bitmaps, inode table, data blocks
 */

#ifndef _EXT2_H
#define _EXT2_H

#include "types.h"

/*
 * ext2 Magic Number
 */
#define EXT2_MAGIC              0xEF53

/*
 * Special Inode Numbers
 */
#define EXT2_ROOT_INO           2       /* Root directory inode */
#define EXT2_FIRST_INO          11      /* First non-reserved inode */

/*
 * File Types (stored in i_mode and directory entries)
 */
#define EXT2_S_IFSOCK           0xC000  /* Socket */
#define EXT2_S_IFLNK            0xA000  /* Symbolic link */
#define EXT2_S_IFREG            0x8000  /* Regular file */
#define EXT2_S_IFBLK            0x6000  /* Block device */
#define EXT2_S_IFDIR            0x4000  /* Directory */
#define EXT2_S_IFCHR            0x2000  /* Character device */
#define EXT2_S_IFIFO            0x1000  /* FIFO (named pipe) */
#define EXT2_S_IFMT             0xF000  /* Type mask */

/*
 * Directory Entry File Types (d_file_type field)
 */
#define EXT2_FT_UNKNOWN         0
#define EXT2_FT_REG_FILE        1
#define EXT2_FT_DIR             2
#define EXT2_FT_CHRDEV          3
#define EXT2_FT_BLKDEV          4
#define EXT2_FT_FIFO            5
#define EXT2_FT_SOCK            6
#define EXT2_FT_SYMLINK         7

/*
 * Superblock - Located at byte offset 1024
 *
 * Contains filesystem-wide parameters. Block size is calculated as:
 *   block_size = 1024 << s_log_block_size
 */
typedef struct {
    uint32_t s_inodes_count;        /* Total inodes */
    uint32_t s_blocks_count;        /* Total blocks */
    uint32_t s_r_blocks_count;      /* Reserved blocks */
    uint32_t s_free_blocks_count;   /* Free blocks */
    uint32_t s_free_inodes_count;   /* Free inodes */
    uint32_t s_first_data_block;    /* First data block (0 for 1KB blocks, 1 for larger) */
    uint32_t s_log_block_size;      /* Block size = 1024 << this */
    uint32_t s_log_frag_size;       /* Fragment size (obsolete) */
    uint32_t s_blocks_per_group;    /* Blocks per group */
    uint32_t s_frags_per_group;     /* Fragments per group (obsolete) */
    uint32_t s_inodes_per_group;    /* Inodes per group */
    uint32_t s_mtime;               /* Last mount time */
    uint32_t s_wtime;               /* Last write time */
    uint16_t s_mnt_count;           /* Mount count since fsck */
    uint16_t s_max_mnt_count;       /* Max mounts before fsck */
    uint16_t s_magic;               /* Magic: 0xEF53 */
    uint16_t s_state;               /* Filesystem state */
    uint16_t s_errors;              /* Error handling behavior */
    uint16_t s_minor_rev_level;     /* Minor revision level */
    uint32_t s_lastcheck;           /* Last fsck time */
    uint32_t s_checkinterval;       /* Max interval between fscks */
    uint32_t s_creator_os;          /* OS that created fs */
    uint32_t s_rev_level;           /* Revision level (0 or 1) */
    uint16_t s_def_resuid;          /* Default UID for reserved blocks */
    uint16_t s_def_resgid;          /* Default GID for reserved blocks */
    /* Rev 1+ fields */
    uint32_t s_first_ino;           /* First non-reserved inode */
    uint16_t s_inode_size;          /* Inode size (128 or 256) */
    uint16_t s_block_group_nr;      /* Block group of this superblock */
    uint32_t s_feature_compat;      /* Compatible features */
    uint32_t s_feature_incompat;    /* Incompatible features */
    uint32_t s_feature_ro_compat;   /* Read-only compatible features */
    uint8_t  s_uuid[16];            /* Filesystem UUID */
    char     s_volume_name[16];     /* Volume name */
    char     s_last_mounted[64];    /* Last mount point */
    uint32_t s_algo_bitmap;         /* Compression algorithm */
    /* Performance hints */
    uint8_t  s_prealloc_blocks;     /* Blocks to preallocate for files */
    uint8_t  s_prealloc_dir_blocks; /* Blocks to preallocate for dirs */
    uint16_t s_reserved_gdt_blocks; /* Reserved GDT blocks for resize */
    /* Padding to 1024 bytes */
    uint8_t  s_reserved[816];
} __attribute__((packed)) ext2_superblock_t;

/*
 * Block Group Descriptor
 *
 * One per block group, stored in the Block Group Descriptor Table
 * immediately after the superblock.
 */
typedef struct {
    uint32_t bg_block_bitmap;       /* Block number of block bitmap */
    uint32_t bg_inode_bitmap;       /* Block number of inode bitmap */
    uint32_t bg_inode_table;        /* Block number of first inode table block */
    uint16_t bg_free_blocks_count;  /* Free blocks in this group */
    uint16_t bg_free_inodes_count;  /* Free inodes in this group */
    uint16_t bg_used_dirs_count;    /* Directories in this group */
    uint16_t bg_pad;                /* Padding */
    uint8_t  bg_reserved[12];       /* Reserved */
} __attribute__((packed)) ext2_group_desc_t;

/*
 * Inode Structure
 *
 * Contains file metadata and block pointers.
 * Size is 128 bytes in rev 0, s_inode_size in rev 1+.
 */
typedef struct {
    uint16_t i_mode;                /* File type and permissions */
    uint16_t i_uid;                 /* Owner UID (low 16 bits) */
    uint32_t i_size;                /* File size in bytes (low 32 bits) */
    uint32_t i_atime;               /* Last access time */
    uint32_t i_ctime;               /* Creation time */
    uint32_t i_mtime;               /* Last modification time */
    uint32_t i_dtime;               /* Deletion time */
    uint16_t i_gid;                 /* Group GID (low 16 bits) */
    uint16_t i_links_count;         /* Hard link count */
    uint32_t i_blocks;              /* Blocks allocated (512-byte units) */
    uint32_t i_flags;               /* File flags */
    uint32_t i_osd1;                /* OS-dependent value 1 */
    uint32_t i_block[15];           /* Block pointers:
                                     *   0-11: Direct blocks
                                     *   12:   Indirect block
                                     *   13:   Double indirect block
                                     *   14:   Triple indirect block
                                     */
    uint32_t i_generation;          /* File version (for NFS) */
    uint32_t i_file_acl;            /* File ACL block */
    uint32_t i_dir_acl;             /* Directory ACL / high 32 bits of size */
    uint32_t i_faddr;               /* Fragment address (obsolete) */
    uint8_t  i_osd2[12];            /* OS-dependent value 2 */
} __attribute__((packed)) ext2_inode_t;

/*
 * Directory Entry
 *
 * Variable-length structure. rec_len is padded to align entries.
 * name is NOT null-terminated on disk.
 */
typedef struct {
    uint32_t inode;                 /* Inode number (0 = unused entry) */
    uint16_t rec_len;               /* Total entry length (for alignment) */
    uint8_t  name_len;              /* Name length */
    uint8_t  file_type;             /* File type (EXT2_FT_*) */
    char     name[];                /* Filename (NOT null-terminated) */
} __attribute__((packed)) ext2_dirent_t;

/*
 * ext2 Filesystem Context
 *
 * Holds pointers to the RAM disk and parsed superblock info.
 */
typedef struct {
    void *disk;                     /* RAM disk base address */
    size_t disk_size;               /* RAM disk size */
    ext2_superblock_t *sb;          /* Superblock pointer */
    ext2_group_desc_t *gdt;         /* Block group descriptor table */
    uint32_t block_size;            /* Block size in bytes */
    uint32_t inodes_per_group;      /* Inodes per block group */
    uint32_t inode_size;            /* Inode structure size */
    uint32_t groups_count;          /* Number of block groups */
} ext2_t;

/*
 * ext2 API
 */

/**
 * ext2_init - Initialize ext2 filesystem on RAM disk
 * @disk: Pointer to RAM disk data
 * @size: Size of RAM disk in bytes
 *
 * Parses the superblock and validates the filesystem.
 *
 * Return: 0 on success, negative errno on failure
 */
int ext2_init(void *disk, size_t size);

/**
 * ext2_read_inode - Read an inode by number
 * @ino: Inode number (1-based, 2 = root directory)
 *
 * Return: Pointer to inode structure, or NULL on error
 */
ext2_inode_t *ext2_read_inode(uint32_t ino);

/**
 * ext2_lookup - Look up a path and return its inode number
 * @path: Absolute path (e.g., "/init" or "/bin/hello")
 *
 * Return: Inode number, or 0 if not found
 */
uint32_t ext2_lookup(const char *path);

/**
 * ext2_read_file - Read file contents
 * @inode: Inode of the file
 * @offset: Byte offset to start reading
 * @buf: Buffer to read into
 * @size: Number of bytes to read
 *
 * Return: Bytes read, or negative errno on failure
 */
ssize_t ext2_read_file(ext2_inode_t *inode, uint64_t offset, void *buf, size_t size);

/**
 * ext2_get_file_size - Get file size from inode
 * @inode: Inode to query
 *
 * Return: File size in bytes
 */
uint64_t ext2_get_file_size(ext2_inode_t *inode);

/**
 * ext2_is_directory - Check if inode is a directory
 * @inode: Inode to check
 *
 * Return: true if directory, false otherwise
 */
bool ext2_is_directory(ext2_inode_t *inode);

/**
 * ext2_is_regular_file - Check if inode is a regular file
 * @inode: Inode to check
 *
 * Return: true if regular file, false otherwise
 */
bool ext2_is_regular_file(ext2_inode_t *inode);

#endif /* _EXT2_H */
