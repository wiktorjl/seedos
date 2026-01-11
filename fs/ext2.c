// SPDX-License-Identifier: GPL-2.0-only
/*
 * ext2 Filesystem Implementation (Read-Only)
 *
 * Minimal implementation for reading files from an ext2 RAM disk.
 * No write support, no caching (RAM disk is already in memory).
 */

#include "ext2.h"
#include "log.h"
#include "syscall_table.h"  /* For errno codes */

/*
 * Global filesystem context
 */
static ext2_t fs;

/*
 * Helper: Get pointer to a block
 */
static void *ext2_block_ptr(uint32_t block_num)
{
    if (block_num == 0)
        return NULL;
    return (uint8_t *)fs.disk + (uint64_t)block_num * fs.block_size;
}

/*
 * Helper: Read a block number from an indirect block
 */
static uint32_t ext2_read_indirect(uint32_t indirect_block, uint32_t index)
{
    uint32_t *block = ext2_block_ptr(indirect_block);
    if (!block)
        return 0;
    return block[index];
}

/*
 * Helper: Get the block number for a given logical block index in a file
 *
 * Handles direct blocks (0-11), indirect (12), double indirect (13),
 * and triple indirect (14).
 */
static uint32_t ext2_get_block_num(ext2_inode_t *inode, uint32_t logical_block)
{
    uint32_t ptrs_per_block = fs.block_size / sizeof(uint32_t);

    /* Direct blocks: 0-11 */
    if (logical_block < 12) {
        return inode->i_block[logical_block];
    }
    logical_block -= 12;

    /* Indirect block: covers next ptrs_per_block blocks */
    if (logical_block < ptrs_per_block) {
        return ext2_read_indirect(inode->i_block[12], logical_block);
    }
    logical_block -= ptrs_per_block;

    /* Double indirect: covers ptrs_per_block^2 blocks */
    if (logical_block < ptrs_per_block * ptrs_per_block) {
        uint32_t idx1 = logical_block / ptrs_per_block;
        uint32_t idx2 = logical_block % ptrs_per_block;
        uint32_t indirect = ext2_read_indirect(inode->i_block[13], idx1);
        return ext2_read_indirect(indirect, idx2);
    }
    logical_block -= ptrs_per_block * ptrs_per_block;

    /* Triple indirect: covers ptrs_per_block^3 blocks */
    if (logical_block < (uint64_t)ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        uint32_t idx1 = logical_block / (ptrs_per_block * ptrs_per_block);
        uint32_t rem = logical_block % (ptrs_per_block * ptrs_per_block);
        uint32_t idx2 = rem / ptrs_per_block;
        uint32_t idx3 = rem % ptrs_per_block;
        uint32_t dbl_indirect = ext2_read_indirect(inode->i_block[14], idx1);
        uint32_t indirect = ext2_read_indirect(dbl_indirect, idx2);
        return ext2_read_indirect(indirect, idx3);
    }

    /* Block number too large */
    return 0;
}

/*
 * Helper: Compare strings with length limit
 */
static int ext2_strncmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return s1[i] - s2[i];
        if (s1[i] == '\0')
            return 0;
    }
    return 0;
}

/*
 * Helper: Get string length
 */
static size_t ext2_strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

/*
 * Helper: Copy memory
 */
static void ext2_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

/**
 * ext2_init - Initialize ext2 filesystem on RAM disk
 */
int ext2_init(void *disk, size_t size)
{
    if (!disk || size < 2048) {
        log_error("EXT2: Invalid disk pointer or size");
        return -EINVAL;
    }

    fs.disk = disk;
    fs.disk_size = size;

    /* Superblock is at byte offset 1024 */
    fs.sb = (ext2_superblock_t *)((uint8_t *)disk + 1024);

    /* Validate magic number */
    if (fs.sb->s_magic != EXT2_MAGIC) {
        log_error("EXT2: Invalid magic 0x%04x (expected 0x%04x)",
                  fs.sb->s_magic, EXT2_MAGIC);
        return -EINVAL;
    }

    /* Calculate block size: 1024 << s_log_block_size */
    fs.block_size = 1024U << fs.sb->s_log_block_size;

    /* Get inode size (128 for rev 0, variable for rev 1+) */
    if (fs.sb->s_rev_level == 0) {
        fs.inode_size = 128;
    } else {
        fs.inode_size = fs.sb->s_inode_size;
    }

    fs.inodes_per_group = fs.sb->s_inodes_per_group;

    /* Calculate number of block groups */
    fs.groups_count = (fs.sb->s_blocks_count + fs.sb->s_blocks_per_group - 1)
                      / fs.sb->s_blocks_per_group;

    /* Block Group Descriptor Table follows the superblock */
    uint32_t gdt_block = (fs.block_size == 1024) ? 2 : 1;
    fs.gdt = ext2_block_ptr(gdt_block);

    log_info("EXT2: Mounted filesystem");
    log_debug("EXT2: Block size: %u, Inode size: %u",
              fs.block_size, fs.inode_size);
    log_debug("EXT2: %u blocks, %u inodes, %u groups",
              fs.sb->s_blocks_count, fs.sb->s_inodes_count, fs.groups_count);

    return 0;
}

/**
 * ext2_read_inode - Read an inode by number
 */
ext2_inode_t *ext2_read_inode(uint32_t ino)
{
    if (ino == 0 || ino > fs.sb->s_inodes_count) {
        log_error("EXT2: Invalid inode number %u", ino);
        return NULL;
    }

    /* Inodes are 1-indexed, convert to 0-indexed */
    uint32_t index = ino - 1;

    /* Find which block group contains this inode */
    uint32_t group = index / fs.inodes_per_group;
    uint32_t local_index = index % fs.inodes_per_group;

    /* Get the inode table block from the group descriptor */
    uint32_t inode_table_block = fs.gdt[group].bg_inode_table;

    /* Calculate offset within inode table */
    uint64_t offset = (uint64_t)local_index * fs.inode_size;
    uint32_t block_offset = offset / fs.block_size;
    uint32_t byte_offset = offset % fs.block_size;

    /* Get pointer to the inode */
    uint8_t *table = ext2_block_ptr(inode_table_block + block_offset);
    if (!table) {
        log_error("EXT2: Cannot read inode table block");
        return NULL;
    }

    return (ext2_inode_t *)(table + byte_offset);
}

/**
 * ext2_lookup_in_dir - Look up a name in a directory
 * @dir_inode: Directory inode
 * @name: Name to find
 * @name_len: Length of name
 *
 * Return: Inode number, or 0 if not found
 */
static uint32_t ext2_lookup_in_dir(ext2_inode_t *dir_inode,
                                   const char *name, size_t name_len)
{
    uint64_t size = ext2_get_file_size(dir_inode);
    uint64_t offset = 0;

    while (offset < size) {
        /* Which logical block? */
        uint32_t logical_block = offset / fs.block_size;
        uint32_t block_offset = offset % fs.block_size;

        uint32_t block_num = ext2_get_block_num(dir_inode, logical_block);
        if (block_num == 0) {
            offset += fs.block_size - block_offset;
            continue;
        }

        uint8_t *block = ext2_block_ptr(block_num);
        ext2_dirent_t *entry = (ext2_dirent_t *)(block + block_offset);

        /* Check each entry in this block */
        while (block_offset < fs.block_size && offset < size) {
            entry = (ext2_dirent_t *)(block + block_offset);

            if (entry->rec_len == 0) {
                /* Corrupted entry */
                break;
            }

            if (entry->inode != 0 &&
                entry->name_len == name_len &&
                ext2_strncmp(entry->name, name, name_len) == 0) {
                return entry->inode;
            }

            block_offset += entry->rec_len;
            offset += entry->rec_len;
        }
    }

    return 0;
}

/**
 * ext2_lookup - Look up a path and return its inode number
 */
uint32_t ext2_lookup(const char *path)
{
    if (!path || path[0] != '/') {
        log_error("EXT2: Invalid path (must be absolute)");
        return 0;
    }

    /* Start at root inode */
    uint32_t ino = EXT2_ROOT_INO;
    const char *p = path + 1;  /* Skip leading '/' */

    while (*p) {
        /* Skip any extra slashes */
        while (*p == '/')
            p++;

        if (*p == '\0')
            break;

        /* Find end of this path component */
        const char *end = p;
        while (*end && *end != '/')
            end++;

        size_t name_len = end - p;

        /* Read current directory inode */
        ext2_inode_t *dir = ext2_read_inode(ino);
        if (!dir) {
            log_error("EXT2: Cannot read inode %u", ino);
            return 0;
        }

        /* Must be a directory */
        if (!ext2_is_directory(dir)) {
            log_error("EXT2: '%.*s' is not a directory", (int)name_len, p);
            return 0;
        }

        /* Look up this component */
        ino = ext2_lookup_in_dir(dir, p, name_len);
        if (ino == 0) {
            log_debug("EXT2: '%.*s' not found", (int)name_len, p);
            return 0;
        }

        p = end;
    }

    return ino;
}

/**
 * ext2_read_file - Read file contents
 */
ssize_t ext2_read_file(ext2_inode_t *inode, uint64_t offset, void *buf, size_t size)
{
    if (!inode || !buf)
        return -EINVAL;

    uint64_t file_size = ext2_get_file_size(inode);

    /* Check if offset is beyond file */
    if (offset >= file_size)
        return 0;

    /* Clamp size to remaining file */
    if (offset + size > file_size)
        size = file_size - offset;

    uint8_t *dst = buf;
    size_t total_read = 0;

    while (size > 0) {
        /* Which logical block? */
        uint32_t logical_block = offset / fs.block_size;
        uint32_t block_offset = offset % fs.block_size;

        /* How much can we read from this block? */
        size_t to_read = fs.block_size - block_offset;
        if (to_read > size)
            to_read = size;

        /* Get the physical block */
        uint32_t block_num = ext2_get_block_num(inode, logical_block);
        if (block_num == 0) {
            /* Sparse file - zero-fill */
            for (size_t i = 0; i < to_read; i++)
                dst[i] = 0;
        } else {
            uint8_t *src = ext2_block_ptr(block_num);
            ext2_memcpy(dst, src + block_offset, to_read);
        }

        dst += to_read;
        offset += to_read;
        size -= to_read;
        total_read += to_read;
    }

    return (ssize_t)total_read;
}

/**
 * ext2_get_file_size - Get file size from inode
 */
uint64_t ext2_get_file_size(ext2_inode_t *inode)
{
    if (!inode)
        return 0;

    /* For regular files, i_dir_acl contains high 32 bits of size */
    if (ext2_is_regular_file(inode)) {
        return ((uint64_t)inode->i_dir_acl << 32) | inode->i_size;
    }

    return inode->i_size;
}

/**
 * ext2_is_directory - Check if inode is a directory
 */
bool ext2_is_directory(ext2_inode_t *inode)
{
    if (!inode)
        return false;
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/**
 * ext2_is_regular_file - Check if inode is a regular file
 */
bool ext2_is_regular_file(ext2_inode_t *inode)
{
    if (!inode)
        return false;
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}
