/*
 * keyboard.c - PS/2 Keyboard Driver
 *
 * This file implements the PS/2 keyboard driver, handling interrupts,
 * scancode translation, and character buffering.
 *
 * Implementation Overview:
 *
 *   1. Interrupt Handling:
 *      - IRQ1 fires when a key is pressed or released
 *      - We read the scancode from port 0x60
 *      - Scancode is translated to ASCII using lookup tables
 *
 *   2. Scancode Translation:
 *      - Set 1 scancodes (XT-compatible) are used
 *      - Two lookup tables: normal and shifted
 *      - Shift key state is tracked separately
 *
 *   3. Character Buffering:
 *      - Circular buffer stores translated ASCII characters
 *      - Producer: interrupt handler (keyboard_handler)
 *      - Consumer: application code (keyboard_get_char)
 *      - Buffer overflow silently drops new characters
 *
 * Circular Buffer Design:
 *
 *   The buffer uses two indices: read_pos and write_pos.
 *   Empty: read_pos == write_pos
 *   Full:  (write_pos + 1) % size == read_pos
 *
 *   This design wastes one slot but avoids the need for a separate
 *   count variable (which would require atomic operations).
 *
 *   ┌───┬───┬───┬───┬───┬───┬───┬───┐
 *   │ a │ b │ c │   │   │   │   │   │
 *   └───┴───┴───┴───┴───┴───┴───┴───┘
 *     ↑           ↑
 *   read        write
 */

#include "keyboard.h"
#include "pic.h"
#include "io.h"
#include "process.h"
#include "sched.h"

/* =============================================================================
 * PS/2 Controller I/O Ports
 *
 * The 8042 PS/2 controller uses two I/O ports for communication.
 * =============================================================================
 */
#define PS2_DATA_PORT    0x60  /* Read: scancodes, Write: commands to keyboard */
#define PS2_STATUS_PORT  0x64  /* Read: status, Write: commands to controller */

/* PS/2 Controller Status Register bits (for future use) */
#define PS2_STATUS_OUTPUT_FULL  (1 << 0)  /* Data available in port 0x60 */
#define PS2_STATUS_INPUT_FULL   (1 << 1)  /* Controller busy, don't write */

/* =============================================================================
 * Keyboard IRQ
 * =============================================================================
 */
#define KEYBOARD_IRQ 1  /* PS/2 keyboard is always on IRQ1 */

/* =============================================================================
 * Scancode Constants (Set 1 / XT-compatible)
 *
 * Key release scancodes have bit 7 set (scancode | 0x80).
 * =============================================================================
 */
#define SCANCODE_RELEASE_BIT    0x80  /* High bit indicates key release */
#define SCANCODE_LEFT_SHIFT     0x2A
#define SCANCODE_RIGHT_SHIFT    0x36
#define SCANCODE_LEFT_SHIFT_UP  0xAA  /* 0x2A | 0x80 */
#define SCANCODE_RIGHT_SHIFT_UP 0xB6  /* 0x36 | 0x80 */

/* =============================================================================
 * Keyboard Input Buffer
 *
 * Circular buffer for storing translated ASCII characters.
 * Volatile because it's shared between interrupt handler and main code.
 * =============================================================================
 */
#define INPUT_BUFFER_SIZE 64

static char input_buffer[INPUT_BUFFER_SIZE];
static volatile int buffer_read_pos = 0;
static volatile int buffer_write_pos = 0;

/* =============================================================================
 * Modifier Key State
 *
 * Track which modifier keys are currently held down.
 * =============================================================================
 */
static int shift_held = 0;

/* =============================================================================
 * Scancode to ASCII Translation Tables (US QWERTY Layout)
 *
 * These tables map Set 1 scancodes to ASCII characters.
 * Index = scancode, value = ASCII character (0 = no printable character)
 *
 * Special characters:
 *   27  = ESC
 *   '\b' = Backspace (0x08)
 *   '\t' = Tab (0x09)
 *   '\n' = Enter (0x0A)
 *
 * Notable scancode assignments:
 *   0x01 = ESC       0x1D = Left Ctrl   0x38 = Left Alt
 *   0x2A = L-Shift   0x36 = R-Shift     0x3A = Caps Lock
 * =============================================================================
 */

/* Unshifted scancode to ASCII mapping */
static const char scancode_to_ascii_normal[] = {
    0,    27,  '1', '2', '3', '4', '5', '6',   /* 0x00-0x07: ESC, numbers    */
    '7', '8', '9', '0', '-', '=', '\b', '\t',  /* 0x08-0x0F: numbers, BS, Tab */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',    /* 0x10-0x17: top row          */
    'o', 'p', '[', ']', '\n', 0,   'a', 's',   /* 0x18-0x1F: Enter, Ctrl, home row */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',    /* 0x20-0x27: home row         */
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',  /* 0x28-0x2F: L-Shift, bottom row */
    'b', 'n', 'm', ',', '.', '/', 0,   '*',    /* 0x30-0x37: bottom row, R-Shift */
    0,   ' ', 0,   0,   0,   0,   0,   0,      /* 0x38-0x3F: Alt, Space, Caps    */
};

/* Shifted scancode to ASCII mapping (with Shift held) */
static const char scancode_to_ascii_shifted[] = {
    0,    27,  '!', '@', '#', '$', '%', '^',   /* 0x00-0x07 */
    '&', '*', '(', ')', '_', '+', '\b', '\t',  /* 0x08-0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',    /* 0x10-0x17 */
    'O', 'P', '{', '}', '\n', 0,   'A', 'S',   /* 0x18-0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',    /* 0x20-0x27 */
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',    /* 0x28-0x2F */
    'B', 'N', 'M', '<', '>', '?', 0,   '*',    /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,      /* 0x38-0x3F */
};

/* Size of scancode tables (for bounds checking) */
#define SCANCODE_TABLE_SIZE (sizeof(scancode_to_ascii_normal))

/* =============================================================================
 * Keyboard API Implementation
 * =============================================================================
 */

/*
 * keyboard_init - Enable keyboard interrupts.
 *
 * The PS/2 keyboard is ready by default after boot. We just need to
 * unmask its IRQ so interrupts can reach our handler.
 */
void keyboard_init(void) {
    pic_unmask(KEYBOARD_IRQ);
}

/*
 * keyboard_handler - Process a keyboard interrupt.
 *
 * This is called from the interrupt handler when IRQ1 fires.
 * We must read the scancode from the data port immediately,
 * even if we don't use it (to acknowledge the keystroke).
 */
void keyboard_handler(void) {
    /* Read scancode - MUST be done to acknowledge the interrupt */
    uint8_t scancode = inb(PS2_DATA_PORT);

    /*
     * Handle shift key press/release.
     * We track shift state separately because it modifies other keys.
     */
    if (scancode == SCANCODE_LEFT_SHIFT || scancode == SCANCODE_RIGHT_SHIFT) {
        shift_held = 1;
        return;
    }
    if (scancode == SCANCODE_LEFT_SHIFT_UP || scancode == SCANCODE_RIGHT_SHIFT_UP) {
        shift_held = 0;
        return;
    }

    /*
     * Ignore key release events (except shift, handled above).
     * Release scancodes have the high bit set.
     */
    if (scancode & SCANCODE_RELEASE_BIT) {
        return;
    }

    /*
     * Translate scancode to ASCII.
     * Use shifted table if shift is held, normal table otherwise.
     */
    char ascii = 0;
    if (scancode < SCANCODE_TABLE_SIZE) {
        if (shift_held) {
            ascii = scancode_to_ascii_shifted[scancode];
        } else {
            ascii = scancode_to_ascii_normal[scancode];
        }
    }

    /*
     * Add character to buffer if it's printable.
     * Silently drop if buffer is full (better than blocking in IRQ handler).
     */
    if (ascii != 0) {
        int next_write_pos = (buffer_write_pos + 1) % INPUT_BUFFER_SIZE;
        if (next_write_pos != buffer_read_pos) {  /* Not full */
            input_buffer[buffer_write_pos] = ascii;
            buffer_write_pos = next_write_pos;
        }
        /* If full, character is dropped - could add overflow counter here */
    }
}

/*
 * keyboard_has_char - Check if any characters are buffered.
 *
 * Returns true if the buffer is not empty.
 */
int keyboard_has_char(void) {
    return buffer_read_pos != buffer_write_pos;
}

/*
 * keyboard_get_char - Remove and return the next character from buffer.
 *
 * Returns: ASCII character, or 0 if buffer is empty.
 */
char keyboard_get_char(void) {
    /* Check if buffer is empty */
    if (buffer_read_pos == buffer_write_pos) {
        return 0;
    }

    /* Read character and advance read position */
    char c = input_buffer[buffer_read_pos];
    buffer_read_pos = (buffer_read_pos + 1) % INPUT_BUFFER_SIZE;
    return c;
}


size_t keyboard_read(char *buf, size_t len) {
    size_t bytes_read = 0;

    while (bytes_read < len) {
        char c = keyboard_get_char();
        if (c == 0) {
            break;  /* No more characters available */
        }
        buf[bytes_read++] = c;
    }
    return bytes_read;
}

/*
 * keyboard_wait - Block until keyboard input is available.
 *
 * This uses a simple polling approach with hlt to reduce CPU usage.
 * The process remains in the scheduler but yields via hlt until
 * keyboard data becomes available.
 *
 * Note: This doesn't fully remove the process from scheduling - it's
 * a cooperative yield. For full blocking I/O, we'd need deeper
 * integration with the scheduler's context switching.
 */
void keyboard_wait(void) {
    /* Fast path: data already available */
    if (keyboard_has_char()) {
        return;
    }

    /*
     * Wait for keyboard input.
     * sti;hlt is atomic on x86 - the interrupt is guaranteed to fire
     * after hlt begins, preventing a race where we enable interrupts,
     * the interrupt fires immediately, and we then halt forever.
     */
    while (!keyboard_has_char()) {
        __asm__ volatile("sti; hlt; cli");
    }

    /* Re-enable interrupts before returning */
    __asm__ volatile("sti");
}