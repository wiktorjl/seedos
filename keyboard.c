/*
 * keyboard.c - PS/2 Keyboard Driver
 */

#include "keyboard.h"
#include "pic.h"
#include "io.h"

/* Keyboard ports */
#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

/* Simple circular buffer for typed characters */
#define KBD_BUFFER_SIZE 64
static char kbd_buffer[KBD_BUFFER_SIZE];
static volatile int kbd_read_pos = 0;
static volatile int kbd_write_pos = 0;

/* Shift state */
static int shift_pressed = 0;

/*
 * US keyboard scancode to ASCII map (set 1)
 * Index = scancode, value = ASCII character (0 = no character)
 */
static const char scancode_to_ascii[] = {
    0,    27,  '1', '2', '3', '4', '5', '6',   /* 0x00-0x07 */
    '7', '8', '9', '0', '-', '=', '\b', '\t',  /* 0x08-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',    /* 0x10-0x17 */
    'o', 'p', '[', ']', '\n', 0,   'a', 's',   /* 0x18-0x1F (0x1D = ctrl) */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',    /* 0x20-0x27 */
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',  /* 0x28-0x2F (0x2A = lshift) */
    'b', 'n', 'm', ',', '.', '/', 0,   '*',    /* 0x30-0x37 (0x36 = rshift) */
    0,   ' ', 0,   0,   0,   0,   0,   0,      /* 0x38-0x3F (0x38 = alt, 0x3A = caps) */
};

/* Shifted version */
static const char scancode_to_ascii_shift[] = {
    0,    27,  '!', '@', '#', '$', '%', '^',   /* 0x00-0x07 */
    '&', '*', '(', ')', '_', '+', '\b', '\t',  /* 0x08-0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',    /* 0x10-0x17 */
    'O', 'P', '{', '}', '\n', 0,   'A', 'S',   /* 0x18-0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',    /* 0x20-0x27 */
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',    /* 0x28-0x2F */
    'B', 'N', 'M', '<', '>', '?', 0,   '*',    /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,      /* 0x38-0x3F */
};

#define SCANCODE_LSHIFT_PRESS   0x2A
#define SCANCODE_RSHIFT_PRESS   0x36
#define SCANCODE_LSHIFT_RELEASE 0xAA
#define SCANCODE_RSHIFT_RELEASE 0xB6

void keyboard_init(void) {
    /* Enable keyboard interrupt (IRQ1) */
    pic_unmask(1);
}

void keyboard_handler(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);
    
    /* Handle shift key */
    if (scancode == SCANCODE_LSHIFT_PRESS || scancode == SCANCODE_RSHIFT_PRESS) {
        shift_pressed = 1;
        return;
    }
    if (scancode == SCANCODE_LSHIFT_RELEASE || scancode == SCANCODE_RSHIFT_RELEASE) {
        shift_pressed = 0;
        return;
    }
    
    /* Ignore key releases (high bit set) */
    if (scancode & 0x80) {
        return;
    }
    
    /* Convert to ASCII */
    char c = 0;
    if (scancode < sizeof(scancode_to_ascii)) {
        if (shift_pressed) {
            c = scancode_to_ascii_shift[scancode];
        } else {
            c = scancode_to_ascii[scancode];
        }
    }
    
    /* Add to buffer if it's a printable character */
    if (c != 0) {
        int next_write = (kbd_write_pos + 1) % KBD_BUFFER_SIZE;
        if (next_write != kbd_read_pos) {  /* Buffer not full */
            kbd_buffer[kbd_write_pos] = c;
            kbd_write_pos = next_write;
        }
    }
}

int keyboard_has_char(void) {
    return kbd_read_pos != kbd_write_pos;
}

char keyboard_get_char(void) {
    if (kbd_read_pos == kbd_write_pos) {
        return 0;  /* Buffer empty */
    }
    char c = kbd_buffer[kbd_read_pos];
    kbd_read_pos = (kbd_read_pos + 1) % KBD_BUFFER_SIZE;
    return c;
}