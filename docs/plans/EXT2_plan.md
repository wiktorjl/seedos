# ext2 Filesystem Implementation Plan for SeedOS

## Overview

Add full read/write ext2 filesystem support mountable on Linux and SeedOS.

**Configuration:**
- Disk driver: ATA/IDE PIO (simple port I/O)
- Block size: 1024 bytes
- Features: files, directories, symlinks, hard links, Unix permissions

## Files to Create

| File | Purpose |
|------|---------|
| `src/ata.h`, `src/ata.c` | ATA/IDE PIO disk driver |
| `src/blkdev.h`, `src/blkdev.c` | Block device abstraction |
| `src/bcache.h`, `src/bcache.c` | Block cache (LRU) |
| `src/ext2.h`, `src/ext2.c` | ext2 filesystem driver |

## Files to Modify

| File | Changes |
|------|---------|
| `src/vfs.h` | Extend vnode_ops with lookup, create, mkdir, unlink, symlink, etc. |
| `src/vfs.c` | Add mount table, path resolution across mounts |
| `src/syscall.h` | Add SYS_MOUNT, SYS_MKDIR, SYS_UNLINK, SYS_LINK, SYS_SYMLINK, etc. |
| `src/syscall.c` | Implement new syscalls |
| `src/process.h` | Add credentials (uid/gid) for permission checks |
| `src/libc/unistd.c` | Implement mkdir, unlink, link, symlink wrappers |
| `Makefile` | Add new source files to C_SOURCES |

---

## Implementation Phases

### Phase 1: ATA/IDE PIO Driver (~250 lines)

**Goal:** Read/write 512-byte sectors from QEMU disk.

**src/ata.h:**
```c
#define ATA_PRIMARY_DATA    0x1F0
#define ATA_PRIMARY_STATUS  0x1F7
#define ATA_CMD_READ        0x20
#define ATA_CMD_WRITE       0x30

struct ata_device {
    uint16_t io_base;
    uint64_t sectors;
    int present;
};

void ata_init(void);
int ata_read_sectors(struct ata_device *dev, uint64_t lba, uint32_t count, void *buf);
int ata_write_sectors(struct ata_device *dev, uint64_t lba, uint32_t count, const void *buf);
```

**Functions:**
- `ata_init()` - Detect primary master using IDENTIFY command
- `ata_wait_ready()` - Poll BSY/DRQ bits
- `ata_read_sectors()` - PIO 28-bit LBA read
- `ata_write_sectors()` - PIO 28-bit LBA write

**Test:** Shell command `disktest` that reads sector 0.

---

### Phase 2: Block Device Abstraction (~100 lines)

**Goal:** Generic block device interface.

**src/blkdev.h:**
```c
struct blkdev {
    const struct blkdev_ops *ops;
    void *private;           // ata_device*
    uint64_t sector_count;
    char name[16];           // "hda"
};

struct blkdev *blkdev_get(const char *name);
int blkdev_read(struct blkdev *dev, uint64_t sector, uint32_t count, void *buf);
int blkdev_write(struct blkdev *dev, uint64_t sector, uint32_t count, const void *buf);
```

---

### Phase 3: Block Cache (~300 lines)

**Goal:** Cache disk blocks in memory (256 blocks = 256KB).

**src/bcache.h:**
```c
#define BCACHE_SIZE 256
#define BLOCK_SIZE 1024

struct buf {
    struct blkdev *dev;
    uint32_t block_num;
    uint8_t data[BLOCK_SIZE];
    uint16_t flags;          // B_VALID, B_DIRTY
    uint16_t refcount;
};

struct buf *bcache_get(struct blkdev *dev, uint32_t block);
void bcache_release(struct buf *b);
void bcache_sync(struct blkdev *dev);
```

**Implementation:** LRU list, static buffer pool.

---

### Phase 4: ext2 Core (~400 lines)

**Goal:** Parse superblock, block groups, read/write inodes.

**Key Structures (src/ext2.h):**
```c
struct ext2_superblock {    // At byte 1024
    uint32_t s_inodes_count, s_blocks_count;
    uint32_t s_free_blocks_count, s_free_inodes_count;
    uint32_t s_first_data_block;     // 1 for 1KB blocks
    uint32_t s_log_block_size;       // 0 = 1KB
    uint32_t s_blocks_per_group, s_inodes_per_group;
    uint16_t s_magic;                // 0xEF53
    uint16_t s_inode_size;           // 128
    // ...
};

struct ext2_group_desc {    // 32 bytes each
    uint32_t bg_block_bitmap, bg_inode_bitmap, bg_inode_table;
    uint16_t bg_free_blocks_count, bg_free_inodes_count;
    // ...
};

struct ext2_inode {         // 128 bytes
    uint16_t i_mode;         // File type + permissions
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_block[15];    // Block pointers (12 direct + 3 indirect)
    uint16_t i_links_count;
    // ...
};

struct ext2_fs {
    struct blkdev *dev;
    struct ext2_superblock sb;
    struct ext2_group_desc *groups;
    uint32_t block_size, group_count;
};
```

**Functions:**
- `ext2_mount(blkdev, fs)` - Read superblock, validate, load group descriptors
- `ext2_read_inode(fs, ino, inode)` - Read inode from inode table
- `ext2_write_inode(fs, ino, inode)` - Write inode back

---

### Phase 5: ext2 Directories (~300 lines)

**Goal:** Parse directories, resolve paths.

```c
struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len, file_type;
    char name[];
};

uint32_t ext2_lookup(ext2_fs *fs, uint32_t dir_ino, const char *name);
uint32_t ext2_namei(ext2_fs *fs, const char *path);  // Path -> inode
int ext2_readdir(ext2_fs *fs, uint32_t dir_ino, ...);
```

---

### Phase 6: ext2 File I/O (~350 lines)

**Goal:** Read/write file data via block pointers.

```c
// Block mapping (handles indirect blocks)
uint32_t ext2_get_block(ext2_fs *fs, ext2_inode *in, uint32_t file_block);
int ext2_set_block(ext2_fs *fs, ext2_inode *in, uint32_t file_block, uint32_t disk_block);

ssize_t ext2_read_file(ext2_fs *fs, uint32_t ino, void *buf, size_t count, off_t off);
ssize_t ext2_write_file(ext2_fs *fs, uint32_t ino, const void *buf, size_t count, off_t off);
```

**Block Pointer Layout:**
- `i_block[0-11]` - Direct blocks (12KB)
- `i_block[12]` - Single indirect (256 pointers = 256KB)
- `i_block[13]` - Double indirect (64MB)
- `i_block[14]` - Triple indirect (16GB)

---

### Phase 7: Block/Inode Allocation (~400 lines)

**Goal:** Allocate/free blocks and inodes.

```c
uint32_t ext2_alloc_block(ext2_fs *fs, uint32_t preferred_group);
void ext2_free_block(ext2_fs *fs, uint32_t block);
uint32_t ext2_alloc_inode(ext2_fs *fs, int is_dir);
void ext2_free_inode(ext2_fs *fs, uint32_t ino);
```

**Implementation:**
- Scan block/inode bitmaps for free bit
- Update superblock and group descriptor free counts
- Mark cache buffers dirty

---

### Phase 8: File/Directory Creation (~500 lines)

**Goal:** Create files, directories, links.

```c
int ext2_create(ext2_fs *fs, uint32_t parent_ino, const char *name, uint16_t mode, uint32_t *new_ino);
int ext2_mkdir(ext2_fs *fs, uint32_t parent_ino, const char *name, uint16_t mode);
int ext2_unlink(ext2_fs *fs, uint32_t dir_ino, const char *name);
int ext2_rmdir(ext2_fs *fs, uint32_t dir_ino, const char *name);
int ext2_link(ext2_fs *fs, uint32_t target_ino, uint32_t dir_ino, const char *name);
int ext2_symlink(ext2_fs *fs, uint32_t dir_ino, const char *name, const char *target);
```

---

### Phase 9: VFS Integration (~400 lines)

**Goal:** Implement extended vnode_ops, mount table.

**Extend src/vfs.h:**
```c
struct vnode_ops {
    ssize_t (*read)(struct vnode *vn, void *buf, size_t count, size_t offset);
    ssize_t (*write)(struct vnode *vn, const void *buf, size_t count, size_t offset);
    int (*close)(struct vnode *vn);
    // New operations:
    int (*lookup)(struct vnode *dir, const char *name, struct vnode **result);
    int (*create)(struct vnode *dir, const char *name, int mode, struct vnode **result);
    int (*mkdir)(struct vnode *dir, const char *name, int mode);
    int (*rmdir)(struct vnode *dir, const char *name);
    int (*unlink)(struct vnode *dir, const char *name);
    int (*link)(struct vnode *dir, struct vnode *target, const char *name);
    int (*symlink)(struct vnode *dir, const char *name, const char *target);
    int (*readlink)(struct vnode *vn, char *buf, size_t bufsiz);
    int (*readdir)(struct vnode *vn, void *buf, size_t count, off_t offset);
    int (*getattr)(struct vnode *vn, struct stat *buf);
    int (*truncate)(struct vnode *vn, off_t length);
};

struct mount {
    char mount_point[256];
    struct vnode *root;
    void *fs_data;         // ext2_fs*
};

int vfs_mount(const char *device, const char *path, const char *type);
int vfs_unmount(const char *path);
struct vnode *vfs_lookup(const char *path);
```

---

### Phase 10: New Syscalls (~550 lines)

**Add to src/syscall.h:**
```c
#define SYS_MOUNT      22
#define SYS_UMOUNT     23
#define SYS_MKDIR      24
#define SYS_RMDIR      25
#define SYS_UNLINK     26
#define SYS_LINK       27
#define SYS_SYMLINK    28
#define SYS_READLINK   29
#define SYS_RENAME     30
#define SYS_CHMOD      31
#define SYS_CHOWN      32
#define SYS_TRUNCATE   33
#define SYS_FTRUNCATE  34
```

---

### Phase 11: Permissions (~200 lines)

**Add to src/process.h:**
```c
struct credentials {
    uint16_t uid, gid;
};
```

**Permission checking:**
```c
int check_permission(struct ext2_inode *inode, uint16_t uid, uint16_t gid, int mode);
```

Integrate into ext2 vnode operations.

---

## Testing

**Create test disk on Linux:**
```bash
dd if=/dev/zero of=disk.img bs=1M count=16
mkfs.ext2 -b 1024 disk.img

mkdir -p mnt && sudo mount -o loop disk.img mnt
echo "Hello ext2" | sudo tee mnt/hello.txt
sudo mkdir mnt/subdir
sudo ln -s hello.txt mnt/symlink.txt
sudo umount mnt
```

**QEMU command:**
```bash
qemu-system-x86_64 -cdrom seed.iso -hda disk.img -serial stdio -m 128M
```

**Test commands in SeedOS shell:**
```
mount hda /mnt ext2
ls /mnt
cat /mnt/hello.txt
echo "Test" > /mnt/newfile.txt
mkdir /mnt/newdir
```

---

## Estimated Effort

| Phase | Lines | Cumulative |
|-------|-------|------------|
| 1. ATA Driver | ~250 | 250 |
| 2. Block Device | ~100 | 350 |
| 3. Block Cache | ~300 | 650 |
| 4. ext2 Core | ~400 | 1050 |
| 5. Directories | ~300 | 1350 |
| 6. File I/O | ~350 | 1700 |
| 7. Allocation | ~400 | 2100 |
| 8. Creation | ~500 | 2600 |
| 9. VFS Integration | ~400 | 3000 |
| 10. Syscalls | ~550 | 3550 |
| 11. Permissions | ~200 | 3750 |

**Total: ~3,750 lines of code**

---

## Makefile Update

```makefile
C_SOURCES += ata.c blkdev.c bcache.c ext2.c

disk.img:
	dd if=/dev/zero of=$@ bs=1M count=16
	mkfs.ext2 -b 1024 $@
```
