# SeedOS Input Drivers Subsystem

## Overview

The PS/2 keyboard driver provides interrupt-driven input with scancode translation.

## PS/2 Keyboard

### Port I/O

```c
#define PS2_DATA   0x60    // Data port
#define PS2_STATUS 0x64    // Status port
```

### Initialization

```c
keyboard_init();  // Called after ioapic_init()
```

Sets up:
1. IRQ handler registration (vector 33)
2. I/O APIC routing (ISA IRQ 1 -> vector 33)
3. Buffer initialization

### API

```c
int keyboard_getchar(void);   // Non-blocking, returns -1 if empty
char keyboard_read(void);     // Blocking until key available
int keyboard_has_input(void); // Check buffer status
```

### Key Buffer

64-character circular buffer with head/tail pointers.

### Modifier Keys

Tracks Shift, Ctrl, Alt, Caps Lock states. Caps Lock only affects letters.

### Special Keys

```c
#define KEY_F1       0x80
#define KEY_UP       0x90
#define KEY_DOWN     0x91
#define KEY_PAGEUP   0x96
#define KEY_PAGEDOWN 0x97
```

### Usage Example (kshell)

```c
for (;;) {
    int c = keyboard_getchar();
    if (c == -1) { kthread_yield(); continue; }
    // Process character
}
```
