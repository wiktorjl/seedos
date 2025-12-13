# The Path to Doom on SeedOS

A roadmap for running the original Doom (1993) on SeedOS.

## What Doom Actually Needs

**Original Doom requirements:**
- 4MB RAM minimum (8MB recommended)
- VGA graphics (320x200, Mode 13h with 256-color palette)
- Keyboard input (key down/up events, not just characters)
- Timer for game loop (~35 tics/second)
- File I/O to read WAD files (~4-12MB)
- Sound (optional - PC speaker or Sound Blaster)
- C runtime (malloc, free, memcpy, file ops, printf-like functions)

## Current SeedOS Gap Analysis

| Requirement | Current State | Gap |
|-------------|---------------|-----|
| **Memory** | PMM/VMM working, sbrk syscall | Need ~8MB+ heap, currently page-at-a-time |
| **Graphics** | Framebuffer (32-bit BGRA likely) | Need palette conversion or port to linear FB |
| **Keyboard** | Basic scancode translation | Need key release events, proper buffering |
| **Timer** | PIT working | Need precise timing API for game loop |
| **Filesystem** | None | **Major gap** - need to read WAD files |
| **C Runtime** | Minimal string.c | Need full libc or custom implementation |
| **ELF Loading** | Working | Need to handle larger binaries |

## Implementation Phases

### Phase 1: Userspace C Environment

Build a minimal C runtime for userspace programs.

**Tasks:**
- [ ] Syscall wrappers in C (`syscall.h` for userspace)
- [ ] Basic libc: `malloc`/`free` wrapping sbrk
- [ ] String functions: `memcpy`, `memset`, `strlen`, `strcpy`, `strcmp`
- [ ] Basic `printf` implementation (or at least `puts`, `putchar`)
- [ ] Verify heap can grow to several MB

**Files to create:**
```
src/userspace/libc/
├── syscall.h    # Syscall wrappers
├── stdlib.h/c   # malloc, free, exit
├── string.h/c   # String functions
├── stdio.h/c    # printf, puts
└── crt0.c       # C runtime startup
```

### Phase 2: Enhanced Keyboard Input

Doom needs to know when keys are pressed AND released.

**Tasks:**
- [ ] Implement key event queue in keyboard driver
- [ ] Track key up (scancode | 0x80) vs key down events
- [ ] Implement `sys_read()` for keyboard fd (fd=0 or dedicated fd)
- [ ] Return key events as structs: `{ scancode, pressed }`
- [ ] Non-blocking read option (return immediately if no input)

**Kernel changes:**
```c
// keyboard.c
struct key_event {
    uint8_t scancode;
    uint8_t pressed;  // 1 = down, 0 = up
};

#define KEY_QUEUE_SIZE 64
static struct key_event key_queue[KEY_QUEUE_SIZE];
static int queue_head, queue_tail;

// sys_read for keyboard
int keyboard_read(struct key_event *buf, int count);
```

### Phase 3: Graphics Primitives

Doom needs direct framebuffer access for performance.

**Option A: Map framebuffer into userspace**
- [ ] Create `sys_fb_info()` syscall returning FB address, width, height, pitch
- [ ] Map framebuffer physical memory into user address space
- [ ] Let userspace write directly to video memory

**Option B: Blit syscall**
- [ ] Create `sys_fb_blit(void *pixels, int x, int y, int w, int h)` syscall
- [ ] Kernel copies user buffer to framebuffer

**Recommended: Option A** (better performance, simpler for Doom)

```c
// syscall.h
struct fb_info {
    uint64_t address;      // Virtual address in userspace
    uint32_t width;
    uint32_t height;
    uint32_t pitch;        // Bytes per row
    uint32_t bpp;          // Bits per pixel
};

int sys_fb_map(struct fb_info *info);  // Maps FB, returns info
```

**Palette handling:**
- Doom uses 256-color palette (8-bit indexed)
- SeedOS framebuffer is likely 32-bit BGRA
- Need conversion in Doom's rendering code OR implement palette syscall

### Phase 4: Filesystem (The Big One)

Need to read DOOM.WAD files. Options from simplest to most complete:

#### Option 1: Embed WAD in Kernel (Hack)
- Link DOOM1.WAD into kernel binary
- Expose via fake file syscalls
- Quick and dirty, good for first test

#### Option 2: Ramdisk via Limine Module (Recommended)
- [ ] Use Limine's module loading to load WAD at boot
- [ ] Implement simple in-memory filesystem
- [ ] Provide `open`, `read`, `lseek`, `close` syscalls

```c
// In kernel, request module from Limine
struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

// Simple VFS for ramdisk
struct file {
    const uint8_t *data;
    size_t size;
    size_t pos;
};

// Syscalls
int sys_open(const char *path, int flags);
ssize_t sys_read(int fd, void *buf, size_t count);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_close(int fd);
```

#### Option 3: FAT Filesystem
- Implement FAT12/16 driver
- Read from disk image
- More complete but significant work

#### Option 4: ISO9660
- Read from CD-ROM image (already booting from it)
- Could load WAD from the same ISO

**Recommended: Option 2** (ramdisk) for first iteration.

### Phase 5: doomgeneric Port

Use the `doomgeneric` project - a portable Doom requiring minimal platform code.

**Repository:** https://github.com/ozkl/doomgeneric

**Required platform functions:**
```c
// doomgeneric_seedos.c

#include "doomgeneric.h"

// Initialize display
void DG_Init() {
    // Map framebuffer, set up palette conversion
}

// Called every frame - copy Doom's buffer to screen
void DG_DrawFrame() {
    // Convert 320x200 paletted to 32-bit BGRA
    // Blit to framebuffer (with scaling if needed)
}

// Sleep for given milliseconds
void DG_SleepMs(uint32_t ms) {
    // Busy wait or proper sleep syscall
}

// Get current time in milliseconds
uint32_t DG_GetTicksMs() {
    return sys_uptime();
}

// Get keyboard input
// Returns 1 if key event available, 0 otherwise
int DG_GetKey(int *pressed, unsigned char *key) {
    struct key_event ev;
    if (sys_read_keyboard(&ev, 1) > 0) {
        *pressed = ev.pressed;
        *key = translate_to_doom_key(ev.scancode);
        return 1;
    }
    return 0;
}

// Optional - set window title (no-op for us)
void DG_SetWindowTitle(const char *title) {
    (void)title;
}
```

**Build integration:**
- Cross-compile doomgeneric with SeedOS userspace toolchain
- Link against mini libc
- Output as ELF, load with existing ELF loader

### Phase 6: Optional Enhancements

After basic Doom runs:

- [ ] **Sound** - PC speaker beeps or Sound Blaster emulation
- [ ] **Mouse support** - PS/2 mouse driver, `sys_read` for mouse events
- [ ] **Save games** - Requires writable filesystem
- [ ] **Networking** - For multiplayer (very ambitious)
- [ ] **Higher resolution** - Scale beyond 320x200

## Architecture Overview

```
┌─────────────────────────────────────────┐
│              Doom (doomgeneric)         │
├─────────────────────────────────────────┤
│   doomgeneric_seedos.c (platform layer) │
│   - DG_DrawFrame → framebuffer write    │
│   - DG_GetKey → sys_read keyboard       │
│   - DG_GetTicksMs → sys_uptime          │
│   - File I/O → sys_open/read/lseek      │
├─────────────────────────────────────────┤
│           Mini libc                     │
│   malloc, free, memcpy, printf, etc.    │
├─────────────────────────────────────────┤
│        Syscall interface (int 0x80)     │
╞═════════════════════════════════════════╡
│                 Kernel                  │
│  ┌─────────┐ ┌─────────┐ ┌───────────┐  │
│  │ FB/Video│ │  VFS    │ │  Keyboard │  │
│  │ (exists)│ │(ramdisk)│ │ (enhance) │  │
│  └─────────┘ └─────────┘ └───────────┘  │
└─────────────────────────────────────────┘
```

## Minimum Viable Doom Checklist

```
[ ] Heap: sbrk supporting multi-MB allocations
[ ] Syscalls: open, read, lseek, close (ramdisk VFS)
[ ] Syscall: fb_map for direct framebuffer access
[ ] Syscall: precise timing (sys_uptime in ms - already exists)
[ ] Keyboard: scan codes with key-up events
[ ] Libc: malloc, free, memcpy, memset, strlen, basic stdio
[ ] WAD loading: ramdisk via Limine module
[ ] doomgeneric integration layer (~200 lines)
[ ] Palette conversion (256-color to 32-bit BGRA)
```

## Complexity Estimates

| Component | Complexity | Approx Lines of Code |
|-----------|------------|----------------------|
| Enhanced keyboard | Low | 100-200 |
| Mini libc | Medium | 500-1000 |
| Ramdisk VFS | Medium | 300-500 |
| FB userspace mapping | Low | 50-100 |
| doomgeneric platform glue | Low | 200-300 |
| **Total new code** | | **~1500-2500** |

## The "Speedrun" Path

Fastest route to "Doom boots":

1. **Embed shareware WAD** in kernel binary (avoid filesystem complexity)
2. **Fake VFS** that reads from embedded blob
3. **Map framebuffer** into userspace (single syscall)
4. **Port doomgeneric** with minimal libc
5. **Accept keyboard limitations** initially (just key-down events)

This bypasses the filesystem entirely and could work with ~1000 lines of new code.

## Resources

- [doomgeneric](https://github.com/ozkl/doomgeneric) - Portable Doom source
- [DOOM1.WAD](https://doomwiki.org/wiki/DOOM1.WAD) - Shareware WAD (legally redistributable)
- [Doom source code](https://github.com/id-Software/DOOM) - Original id Software release
- [Limine Protocol](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md) - Module loading docs

## Success Criteria

Doom is "running" when:
1. Title screen displays correctly
2. Menu is navigable with keyboard
3. Game starts and renders first level (E1M1)
4. Player can move and shoot
5. Enemies react and game is playable

Sound is optional. Saving is optional. Full WAD support is optional.

**The goal: See the Doom title screen on SeedOS.**
