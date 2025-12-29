/*
 * keyboard.c - PS/2 Keyboard Driver
 *
 * Handles keyboard interrupts and translates scancodes to ASCII.
 * Uses a circular buffer for input.
 */

#include "keyboard.h"
#include "ioapic.h"
#include "apic.h"
#include "idt.h"
#include "log.h"

/* =============================================================================
 * PS/2 Controller Ports
 * =============================================================================
 */

#define PS2_DATA        0x60    /* Data port (read scancode, write command data) */
#define PS2_STATUS      0x64    /* Status port (read) */
#define PS2_COMMAND     0x64    /* Command port (write) */

/* Status register bits */
#define PS2_STATUS_OUTPUT   0x01    /* Output buffer full (can read) */
#define PS2_STATUS_INPUT    0x02    /* Input buffer full (don't write) */

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* =============================================================================
 * Scancode Set 1 to ASCII Translation
 *
 * Standard US QWERTY layout. Index is scancode, value is ASCII.
 * 0 means no translation (special key or unused).
 * =============================================================================
 */

static const char scancode_to_ascii[128] = {
    0,    KEY_ESCAPE, '1', '2', '3', '4', '5', '6',     /* 0x00-0x07 */
    '7',  '8', '9', '0', '-', '=', KEY_BACKSPACE, KEY_TAB, /* 0x08-0x0F */
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',             /* 0x10-0x17 */
    'o',  'p', '[', ']', KEY_ENTER, 0, 'a', 's',         /* 0x18-0x1F (0x1D = Left Ctrl) */
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',             /* 0x20-0x27 */
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',              /* 0x28-0x2F (0x2A = Left Shift) */
    'b',  'n', 'm', ',', '.', '/', 0, '*',               /* 0x30-0x37 (0x36 = Right Shift) */
    0,    ' ', 0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, /* 0x38-0x3F (0x38 = Left Alt, 0x3A = Caps) */
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, KEY_HOME, /* 0x40-0x47 */
    KEY_UP, KEY_PAGEUP, '-', KEY_LEFT, 0, KEY_RIGHT, '+', KEY_END, /* 0x48-0x4F */
    KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, KEY_F11, /* 0x50-0x57 */
    KEY_F12, 0, 0, 0, 0, 0, 0, 0,                        /* 0x58-0x5F */
    0, 0, 0, 0, 0, 0, 0, 0,                              /* 0x60-0x67 */
    0, 0, 0, 0, 0, 0, 0, 0,                              /* 0x68-0x6F */
    0, 0, 0, 0, 0, 0, 0, 0,                              /* 0x70-0x77 */
    0, 0, 0, 0, 0, 0, 0, 0                               /* 0x78-0x7F */
};

/* Shifted versions of keys */
static const char scancode_to_ascii_shift[128] = {
    0,    KEY_ESCAPE, '!', '@', '#', '$', '%', '^',     /* 0x00-0x07 */
    '&',  '*', '(', ')', '_', '+', KEY_BACKSPACE, KEY_TAB, /* 0x08-0x0F */
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',             /* 0x10-0x17 */
    'O',  'P', '{', '}', KEY_ENTER, 0, 'A', 'S',         /* 0x18-0x1F */
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',             /* 0x20-0x27 */
    '"',  '~', 0, '|', 'Z', 'X', 'C', 'V',               /* 0x28-0x2F */
    'B',  'N', 'M', '<', '>', '?', 0, '*',               /* 0x30-0x37 */
    0,    ' ', 0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, /* 0x38-0x3F */
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, KEY_HOME,
    KEY_UP, KEY_PAGEUP, '-', KEY_LEFT, 0, KEY_RIGHT, '+', KEY_END,
    KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, KEY_F11,
    KEY_F12, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* =============================================================================
 * Keyboard State
 * =============================================================================
 */

static volatile char buffer[KBD_BUFFER_SIZE];
static volatile int buffer_head;    /* Write position */
static volatile int buffer_tail;    /* Read position */

/* Modifier key state */
static volatile int shift_pressed;
static volatile int ctrl_pressed;
static volatile int alt_pressed;
static volatile int caps_lock;

/* =============================================================================
 * Buffer Operations
 * =============================================================================
 */

static void buffer_put(char c) {
    int next = (buffer_head + 1) % KBD_BUFFER_SIZE;
    if (next != buffer_tail) {  /* Buffer not full */
        buffer[buffer_head] = c;
        buffer_head = next;
    }
    /* If full, drop the character */
}

static int buffer_get(void) {
    if (buffer_head == buffer_tail) {
        return -1;  /* Buffer empty */
    }
    char c = buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KBD_BUFFER_SIZE;
    return (unsigned char)c;
}

/* =============================================================================
 * Keyboard Interrupt Handler
 * =============================================================================
 */

static void keyboard_irq_handler(interrupt_frame_t *frame) {
    (void)frame;

    /* Read scancode from PS/2 data port */
    uint8_t scancode = inb(PS2_DATA);

    /* Check for key release (bit 7 set) */
    int released = scancode & 0x80;
    scancode &= 0x7F;  /* Clear release bit */

    /* Handle modifier keys */
    switch (scancode) {
        case 0x2A:  /* Left Shift */
        case 0x36:  /* Right Shift */
            shift_pressed = !released;
            goto done;
        case 0x1D:  /* Left Ctrl */
            ctrl_pressed = !released;
            goto done;
        case 0x38:  /* Left Alt */
            alt_pressed = !released;
            goto done;
        case 0x3A:  /* Caps Lock */
            if (!released) {
                caps_lock = !caps_lock;
            }
            goto done;
    }

    /* Only process key presses, not releases */
    if (released) {
        goto done;
    }

    /* Translate scancode to ASCII */
    char c;
    int use_shift = shift_pressed ^ caps_lock;  /* XOR for caps lock toggle */

    /* Caps lock only affects letters */
    if (scancode_to_ascii[scancode] >= 'a' && scancode_to_ascii[scancode] <= 'z') {
        c = use_shift ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    } else {
        c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    }

    if (c != 0) {
        /* Handle Ctrl+letter (produce control characters) */
        if (ctrl_pressed && c >= 'a' && c <= 'z') {
            c = c - 'a' + 1;  /* Ctrl+A = 1, Ctrl+B = 2, etc. */
        } else if (ctrl_pressed && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 1;
        }

        buffer_put(c);
    }

done:
    apic_eoi();
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

void keyboard_init(void) {
    /* Initialize state */
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;

    /* Register interrupt handler */
    idt_register_irq(IRQ_KEYBOARD, keyboard_irq_handler);

    /* Route keyboard IRQ (ISA IRQ 1) to vector 33, CPU 0 */
    ioapic_route_irq(ISA_IRQ_KEYBOARD, IRQ_KEYBOARD, 0);

    /* Clear any pending data in the keyboard buffer */
    while (inb(PS2_STATUS) & PS2_STATUS_OUTPUT) {
        inb(PS2_DATA);
    }

    log_info("KEYBOARD: Initialized (vector %d)", IRQ_KEYBOARD);
}

int keyboard_getchar(void) {
    return buffer_get();
}

char keyboard_read(void) {
    int c;
    while ((c = keyboard_getchar()) == -1) {
        asm volatile("hlt");  /* Wait for interrupt */
    }
    return (char)c;
}

int keyboard_has_input(void) {
    return buffer_head != buffer_tail;
}
