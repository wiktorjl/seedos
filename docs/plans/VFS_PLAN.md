# Ramdisk VFS Implementation Plan

A plan for implementing a minimal Virtual File System with TAR-based initrd support for SeedOS.

## Overview

### What is a VFS?

A Virtual File System is an abstraction layer that provides a uniform interface for file operations regardless of the underlying storage mechanism. It allows:
- Consistent syscall interface (`open`, `read`, `write`, `close`, `lseek`)
- Multiple filesystem types behind one API
- File descriptor management

### Why Ramdisk First?

A ramdisk is the simplest filesystem to implement:
- No disk driver needed
- No complex on-disk structures (FAT, ext2, etc.)
- Data already in memory (loaded by bootloader)
- Perfect for loading game assets, config files, initial programs

### Why TAR Format?

For userspace binaries and shell functionality, a TAR-based initrd is ideal:
- **Directory structure** - Supports `bin/`, `etc/`, hierarchical layout
- **File enumeration** - Shell can `ls` to list available programs
- **Standard format** - Create archives with normal `tar` command
- **Simple parsing** - ~100 lines of kernel code
- **Single module** - One Limine module instead of many

**The Goal:**
```
# In the shell
> ls
bin/
  hello
  info
  count
  doom

> run hello
Hello, World!
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   User Space                        │
│   open("/bin/hello")  read(fd, buf, n)  close(fd)  │
└─────────────────────────┬───────────────────────────┘
                          │ int 0x80
┌─────────────────────────▼───────────────────────────┐
│                  Syscall Layer                      │
│        sys_open  sys_read  sys_write  sys_close    │
│        sys_getdents (directory listing)            │
└─────────────────────────┬───────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────┐
│                    VFS Layer                        │
│  ┌─────────────────────────────────────────────┐   │
│  │  File Descriptor Table (per-process)        │   │
│  │  fd -> { vnode, position, flags }           │   │
│  └─────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────┐   │
│  │  Vnode Table (global)                       │   │
│  │  vnode -> { fs_ops, fs_data, refcount }     │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────┬───────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────┐
│              Filesystem Drivers                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────┐  │
│  │  TAR Initrd  │  │   DevFS      │  │  (Future)│  │
│  │  (files)     │  │  (devices)   │  │   FAT    │  │
│  └──────────────┘  └──────────────┘  └──────────┘  │
└─────────────────────────────────────────────────────┘
```

## Initrd Structure

```
initrd/
├── bin/
│   ├── hello      # ELF binary
│   ├── info       # ELF binary
│   ├── count      # ELF binary
│   ├── heap       # ELF binary
│   └── doom       # ELF binary (eventually)
├── etc/
│   └── motd       # Message of the day
└── data/
    └── doom1.wad  # Game assets
```

## TAR Format

TAR is surprisingly simple - just 512-byte headers followed by file data:

```
┌─────────────────────┐
│  512-byte header    │  ← filename, size, type
├─────────────────────┤
│  file data          │  ← rounded up to 512 bytes
│  (padded to 512)    │
├─────────────────────┤
│  512-byte header    │  ← next file
├─────────────────────┤
│  file data          │
├─────────────────────┤
│  ... more files ... │
├─────────────────────┤
│  two zero blocks    │  ← end of archive
└─────────────────────┘
```

### TAR Header Structure

```c
// tar.h
#define TAR_BLOCK_SIZE 512

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

// Type flags
#define TAR_TYPE_FILE     '0'
#define TAR_TYPE_DIR      '5'
```

### TAR Parser Implementation

```c
// tar.c

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

// Parsed file entry
struct tar_file {
    char name[100];
    const uint8_t *data;
    size_t size;
    uint8_t is_dir;
};

#define MAX_TAR_FILES 64
static struct tar_file tar_files[MAX_TAR_FILES];
static int tar_file_count = 0;

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
```

## Data Structures

### File Descriptor (per-process)

```c
// vfs.h

#define MAX_FDS 16          // Max open files per process
#define MAX_VNODES 64       // Max files system-wide

// Open file descriptor
struct file_descriptor {
    struct vnode *vnode;    // Pointer to vnode (NULL if fd unused)
    size_t position;        // Current read/write position
    int flags;              // O_RDONLY, O_WRONLY, O_RDWR
};

// Per-process file descriptor table
struct fd_table {
    struct file_descriptor fds[MAX_FDS];
};
```

### Vnode (Virtual Node)

```c
// Represents a file/directory in the VFS
struct vnode {
    const struct vnode_ops *ops;  // Filesystem operations
    void *fs_data;                // Filesystem-specific data
    int refcount;                 // Number of open fds pointing here
    uint32_t type;                // VNODE_FILE, VNODE_DIR, VNODE_DEV
    size_t size;                  // File size in bytes
};

// Vnode types
#define VNODE_FILE  1
#define VNODE_DIR   2
#define VNODE_DEV   3

// Operations each filesystem must implement
struct vnode_ops {
    ssize_t (*read)(struct vnode *vn, void *buf, size_t count, size_t offset);
    ssize_t (*write)(struct vnode *vn, const void *buf, size_t count, size_t offset);
    int (*getdents)(struct vnode *vn, struct dirent *buf, size_t count);
    int (*close)(struct vnode *vn);
};
```

## Syscall Interface

### sys_open

```c
// Open a file, return file descriptor or negative error
int sys_open(const char *path, int flags);

// Flags
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_DIRECTORY 0x10000  // Expect a directory

// Returns: fd (3-15) on success, negative on error
// Note: fds 0, 1, 2 reserved for stdin/stdout/stderr
// Errors: -ENOENT (not found), -EMFILE (too many open files)
```

### sys_read

```c
// Read from file descriptor
ssize_t sys_read(int fd, void *buf, size_t count);

// Returns: bytes read (0 at EOF), negative on error
// Errors: -EBADF (bad fd), -EFAULT (bad buffer pointer)
```

### sys_write

```c
// Write to file descriptor
ssize_t sys_write(int fd, const void *buf, size_t count);

// Returns: bytes written, negative on error
// Note: TAR initrd is read-only, write returns -EROFS
// Exception: fd 1 (stdout) writes to console (existing behavior)
```

### sys_lseek

```c
// Reposition file offset
off_t sys_lseek(int fd, off_t offset, int whence);

// Whence values
#define SEEK_SET  0   // Offset from beginning
#define SEEK_CUR  1   // Offset from current position
#define SEEK_END  2   // Offset from end

// Returns: new position, negative on error
```

### sys_close

```c
// Close file descriptor
int sys_close(int fd);

// Returns: 0 on success, negative on error
```

### sys_getdents (for directory listing)

```c
// Get directory entries
int sys_getdents(int fd, struct dirent *buf, size_t count);

struct dirent {
    uint32_t d_ino;       // Inode number (can be index)
    uint16_t d_reclen;    // Length of this record
    uint8_t  d_type;      // File type (DT_REG, DT_DIR)
    char     d_name[256]; // Filename
};

#define DT_REG  8   // Regular file
#define DT_DIR  4   // Directory

// Returns: bytes read into buf, 0 at end, negative on error
```

## Shell Integration

### ls Command

```c
// shell.c

static void print_dir_entry(const char *name, size_t size, int is_dir, void *ctx) {
    (void)ctx;
    if (is_dir) {
        puts("  [DIR]  ");
    } else {
        puts("  ");
        put_dec(size);
        puts("\t ");
    }
    puts(name);
    puts("\n");
}

void cmd_ls(const char *path) {
    if (!path || !*path) path = "bin";

    puts("Contents of /");
    puts(path);
    puts("/:\n");

    int count = tar_list_dir(path, print_dir_entry, NULL);

    if (count == 0) {
        puts("  (empty)\n");
    }
}
```

### run Command (Updated)

```c
// shell.c

void cmd_run(const char *name) {
    // Build path: "bin/" + name
    char path[64] = "bin/";
    strcat(path, name);

    struct tar_file *file = tar_find(path);
    if (!file) {
        puts("Program not found: ");
        puts(name);
        puts("\nUse 'ls' to see available programs\n");
        return;
    }

    if (file->is_dir) {
        puts("Cannot run a directory\n");
        return;
    }

    // Load and run ELF from TAR data
    puts("Running ");
    puts(name);
    puts("...\n\n");

    int result = process_run_elf(file->data, file->size);

    puts("\nProcess exited with code ");
    put_dec(result);
    puts("\n");
}
```

## Build Process

### Updated limine.conf

```
timeout: 0

/Seed OS
    protocol: limine
    path: boot():/boot/kernel.elf
    module_path: boot():/boot/initrd.tar
```

### Updated build.sh

```bash
#!/bin/bash
set -e

# ... existing setup ...

# Build userspace programs
echo "Building userspace programs..."
mkdir -p "$BUILD_DIR/initrd/bin"

USERSPACE_PROGRAMS="hello info count heap alpha stars"

for prog in $USERSPACE_PROGRAMS; do
    echo "  Compiling $prog..."
    x86_64-elf-gcc \
        -ffreestanding -nostdlib -static \
        -Tsrc/userspace/user.ld \
        -o "$BUILD_DIR/initrd/bin/$prog" \
        src/userspace/$prog.c \
        src/userspace/libc/*.c
done

# Create initrd tarball
echo "Creating initrd.tar..."
tar -cf "$BUILD_DIR/iso_root/boot/initrd.tar" \
    -C "$BUILD_DIR/initrd" \
    --format=ustar \
    .

# ... rest of ISO creation ...
```

### Kernel Initialization

```c
// kernel.c

static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

void kernel_main(void) {
    // ... existing init ...

    // Initialize TAR filesystem from initrd
    struct limine_module_response *mod_resp = module_request.response;
    if (mod_resp && mod_resp->module_count > 0) {
        struct limine_file *initrd = mod_resp->modules[0];
        tar_init(initrd->address, initrd->size);
        puts("Initrd loaded: ");
        put_dec(tar_get_file_count());
        puts(" files\n");
    } else {
        puts("Warning: No initrd loaded\n");
    }

    // ... shell ...
}
```

## Implementation Phases

### Phase 1: TAR Parser

**Goal:** Parse TAR archive loaded by Limine.

**Tasks:**
- [ ] Create `src/tar.h` with TAR structures
- [ ] Create `src/tar.c` with parser
- [ ] Implement `tar_init()` - parse archive into file list
- [ ] Implement `tar_find()` - lookup by path
- [ ] Implement `tar_list_dir()` - enumerate directory
- [ ] Test: print file list at boot

### Phase 2: Shell Commands

**Goal:** `ls` and updated `run` commands work.

**Tasks:**
- [ ] Add `ls` command to shell
- [ ] Update `run` to load from TAR instead of embedded programs
- [ ] Update `programs` to list from TAR
- [ ] Remove embedded program registry (programs.c)

### Phase 3: Core VFS Infrastructure

**Goal:** Basic data structures and fd management.

**Tasks:**
- [ ] Create `src/vfs.h` with data structures
- [ ] Create `src/vfs.c` with fd table management
- [ ] Implement `vfs_alloc_fd()` - find free fd slot
- [ ] Implement `vfs_free_fd()` - release fd
- [ ] Implement `vfs_get_fd()` - get fd_table entry by number
- [ ] Add `struct fd_table` to process structure

### Phase 4: TAR VFS Driver

**Goal:** TAR files accessible via VFS.

**Tasks:**
- [ ] Create `src/tarfs.c` with TAR vnode operations
- [ ] Implement `tarfs_open()` - create vnode for TAR file
- [ ] Implement `tarfs_read()` - read from TAR file data
- [ ] Implement `tarfs_getdents()` - directory listing
- [ ] Implement `tarfs_close()` - cleanup

### Phase 5: Syscall Implementation

**Goal:** Wire up syscalls to VFS layer.

**Tasks:**
- [ ] Add syscall numbers to `syscall.h`
- [ ] Implement `sys_open()` in syscall.c
- [ ] Implement `sys_read()` for files (enhance existing)
- [ ] Implement `sys_lseek()` in syscall.c
- [ ] Implement `sys_close()` in syscall.c
- [ ] Implement `sys_getdents()` for directories
- [ ] Validate user pointers for all syscalls
- [ ] Handle special fds (0=stdin, 1=stdout, 2=stderr)

**Syscall numbers:**
```c
#define SYS_READ     0   // Existing, enhance for files
#define SYS_WRITE    1   // Existing
#define SYS_OPEN     2   // New
#define SYS_CLOSE    3   // New
#define SYS_LSEEK    8   // New
#define SYS_GETDENTS 78  // New (directory listing)
#define SYS_GETPID   39  // Existing
#define SYS_EXIT     60  // Existing
#define SYS_SBRK     12  // Existing
#define SYS_UPTIME   96  // Existing
```

### Phase 6: Process Integration

**Goal:** Each process has its own fd table.

**Tasks:**
- [ ] Add `struct fd_table *fds` to process struct
- [ ] Allocate fd_table on process creation
- [ ] Initialize stdin/stdout/stderr (fds 0, 1, 2)
- [ ] Free fd_table and close all fds on process exit
- [ ] Ensure syscalls use current process's fd_table

### Phase 7: Userspace Library

**Goal:** C wrappers for file syscalls.

**Tasks:**
- [ ] Add syscall wrappers to userspace libc
- [ ] Create test program that reads a file
- [ ] Create test program that lists a directory

### Phase 8: Build System Update

**Goal:** Automated initrd creation.

**Tasks:**
- [ ] Update Makefile to compile userspace programs
- [ ] Update build.sh to create initrd.tar
- [ ] Update limine.conf for single module
- [ ] Remove embedded programs from kernel
- [ ] Test full build and boot

## Error Codes

```c
// errno.h
#define ENOENT   2    // No such file or directory
#define EBADF    9    // Bad file descriptor
#define EFAULT  14    // Bad address (invalid pointer)
#define ENOTDIR 20    // Not a directory
#define EISDIR  21    // Is a directory
#define EINVAL  22    // Invalid argument
#define EMFILE  24    // Too many open files
#define EROFS   30    // Read-only file system
```

## Special File Descriptors

Standard Unix convention:
- **fd 0**: stdin (keyboard input)
- **fd 1**: stdout (console output)
- **fd 2**: stderr (console output, same as stdout for now)

These are pre-opened for every process:
- `read(0, ...)` reads from keyboard
- `write(1, ...)` and `write(2, ...)` write to console

```c
void fd_table_init(struct fd_table *fdt) {
    memset(fdt, 0, sizeof(*fdt));
    fdt->fds[0].vnode = &stdin_vnode;   // Keyboard device
    fdt->fds[1].vnode = &stdout_vnode;  // Console device
    fdt->fds[2].vnode = &stdout_vnode;  // Console device
    fdt->fds[0].flags = O_RDONLY;
    fdt->fds[1].flags = O_WRONLY;
    fdt->fds[2].flags = O_WRONLY;
}
```

## File Structure

```
src/
├── vfs.h           # VFS data structures and API
├── vfs.c           # VFS core (fd management, dispatch)
├── tar.h           # TAR format structures
├── tar.c           # TAR parser
├── tarfs.c         # TAR filesystem vnode operations
├── errno.h         # Error codes
└── userspace/
    └── libc/
        ├── unistd.h    # File syscall wrappers
        ├── fcntl.h     # O_RDONLY, O_DIRECTORY, etc.
        ├── dirent.h    # struct dirent, DT_REG, DT_DIR
        └── errno.h     # Userspace error codes
```

## Complexity Estimate

| Component | Lines of Code | Complexity |
|-----------|---------------|------------|
| tar.h/tar.c | 100-150 | Low |
| vfs.h | 50-80 | Low |
| vfs.c | 100-150 | Medium |
| tarfs.c | 80-120 | Low |
| syscall additions | 100-150 | Medium |
| shell updates | 50-80 | Low |
| process integration | 30-50 | Low |
| userspace wrappers | 50-80 | Low |
| build system | 30-50 | Low |
| **Total** | **~600-900** | |

## Migration Path

1. **Keep current embedded programs working** during development
2. **Add TAR parser** - test by printing file list at boot
3. **Add shell `ls` command** - list TAR contents
4. **Update `run` command** - load from TAR instead of embedded
5. **Build initrd.tar** - compile userspace to TAR
6. **Remove embedded programs** - clean up programs.c
7. **Add VFS layer** - for proper file syscalls
8. **Add userspace file API** - open/read/close wrappers

## Testing

### Boot Test
```
Seed OS v0.1
Initrd loaded: 8 files

> ls
Contents of /bin/:
  1234     hello
  2048     info
  896      count

3 entries

> run hello
Running hello...

Hello, World!

Process exited with code 0
```

## Quick Start Checklist

Minimum for shell file listing:

```
[ ] TAR parser implemented and tested
[ ] Limine loads initrd.tar module
[ ] tar_init() called at kernel boot
[ ] Shell 'ls' command works
[ ] Shell 'run' loads from TAR
[ ] Userspace programs compile to initrd
```

Full VFS for userspace file access:

```
[ ] VFS layer with fd tables
[ ] sys_open returns fd for TAR files
[ ] sys_read reads TAR file data
[ ] sys_lseek repositions in file
[ ] sys_close releases fd
[ ] sys_getdents lists directories
[ ] Per-process fd tables
[ ] Userspace libc wrappers
```

## Future Extensions

Once TAR-based initrd VFS works:

1. **DevFS** - `/dev/null`, `/dev/zero`, `/dev/fb0` (framebuffer)
2. **ProcFS** - `/proc/self/pid`, process info
3. **FAT Driver** - Read real disk images
4. **Write support** - Writable ramdisk or disk
5. **Path resolution** - Handle relative paths, `..`, symlinks
6. **Mount points** - Mount different filesystems at paths

## References

- [TAR Format Specification](https://www.gnu.org/software/tar/manual/html_node/Standard.html)
- [POSIX ustar Format](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html)
- [Limine Protocol - Modules](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md)
- [OSDev Wiki - VFS](https://wiki.osdev.org/VFS)
- [xv6 File System](https://github.com/mit-pdos/xv6-public/blob/master/fs.c)
