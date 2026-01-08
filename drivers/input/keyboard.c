// SPDX-License-Identifier: GPL-2.0-only
/*
 * PS/2 Keyboard Driver
 *
 * Interrupt-driven keyboard input with scancode-to-ASCII translation.
 */

#include "keyboard.h"
#include "io.h"
#include "ioapic.h"
#include "apic.h"
#include "idt.h"
#include "log.h"

/* PS/2 controller ports */
#define PS2_DATA        0x60
#define PS2_STATUS      0x64
#define PS2_COMMAND     0x64

#define PS2_STATUS_OUTPUT   0x01
#define PS2_STATUS_INPUT    0x02

/* Scancode Set 1 to ASCII - US QWERTY layout */
static const char scancode_to_ascii[128] = {
    0,    KEY_ESCAPE, '1', '2', '3', '4', '5', '6',
    '7',  '8', '9', '0', '-', '=', KEY_BACKSPACE, KEY_TAB,
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o',  'p', '[', ']', KEY_ENTER, 0, 'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '/', 0, '*',
    0,    ' ', 0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, KEY_HOME,
    KEY_UP, KEY_PAGEUP, '-', KEY_LEFT, 0, KEY_RIGHT, '+', KEY_END,
    KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, KEY_F11,
    KEY_F12, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_to_ascii_shift[128] = {
    0,    KEY_ESCAPE, '!', '@', '#', '$', '%', '^',
    '&',  '*', '(', ')', '_', '+', KEY_BACKSPACE, KEY_TAB,
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P', '{', '}', KEY_ENTER, 0, 'A', 'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"',  '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B',  'N', 'M', '<', '>', '?', 0, '*',
    0,    ' ', 0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, KEY_HOME,
    KEY_UP, KEY_PAGEUP, '-', KEY_LEFT, 0, KEY_RIGHT, '+', KEY_END,
    KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, KEY_F11,
    KEY_F12, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static volatile char buffer[KBD_BUFFER_SIZE];
static volatile int buffer_head;
static volatile int buffer_tail;
static volatile int shift_pressed;
static volatile int ctrl_pressed;
static volatile int alt_pressed;
static volatile int caps_lock;

static void buffer_put(char c)
{
    int next = (buffer_head + 1) % KBD_BUFFER_SIZE;
    if (next != buffer_tail) {
        buffer[buffer_head] = c;
        buffer_head = next;
    }
}

static int buffer_get(void)
{
    if (buffer_head == buffer_tail)
        return -1;
    char c = buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KBD_BUFFER_SIZE;
    return (unsigned char)c;
}

static void keyboard_irq_handler(interrupt_frame_t *frame)
{
    (void)frame;

    uint8_t scancode = inb(PS2_DATA);
    int released = scancode & 0x80;
    scancode &= 0x7F;

    switch (scancode) {
        case 0x2A:
        case 0x36:
            shift_pressed = !released;
            goto done;
        case 0x1D:
            ctrl_pressed = !released;
            goto done;
        case 0x38:
            alt_pressed = !released;
            goto done;
        case 0x3A:
            if (!released)
                caps_lock = !caps_lock;
            goto done;
    }

    if (released)
        goto done;

    char c;
    int use_shift = shift_pressed ^ caps_lock;

    if (scancode_to_ascii[scancode] >= 'a' && scancode_to_ascii[scancode] <= 'z')
        c = use_shift ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    else
        c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];

    if (c != 0) {
        if (ctrl_pressed && c >= 'a' && c <= 'z')
            c = c - 'a' + 1;
        else if (ctrl_pressed && c >= 'A' && c <= 'Z')
            c = c - 'A' + 1;

        buffer_put(c);
    }

done:
    apic_eoi();
}

/**
 * keyboard_init - Initialize the PS/2 keyboard driver
 *
 * Registers keyboard IRQ handler and routes via I/O APIC.
 * Must be called after ioapic_init().
 */
void keyboard_init(void)
{
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;

    idt_register_irq(IRQ_KEYBOARD, keyboard_irq_handler);
    ioapic_route_irq(ISA_IRQ_KEYBOARD, IRQ_KEYBOARD, 0);

    while (inb(PS2_STATUS) & PS2_STATUS_OUTPUT)
        inb(PS2_DATA);

    log_info("KEYBOARD: vector %d", IRQ_KEYBOARD);
}

/**
 * keyboard_getchar - Get a character from the keyboard buffer
 *
 * Return: ASCII character, or -1 if buffer is empty
 */
int keyboard_getchar(void)
{
    return buffer_get();
}

/**
 * keyboard_read - Read a character, blocking until available
 *
 * Return: ASCII character
 */
char keyboard_read(void)
{
    int c;
    while ((c = keyboard_getchar()) == -1)
        __asm__ volatile("hlt");
    return (char)c;
}

/**
 * keyboard_has_input - Check if keyboard input is available
 *
 * Return: non-zero if input pending, 0 otherwise
 */
int keyboard_has_input(void)
{
    return buffer_head != buffer_tail;
}
