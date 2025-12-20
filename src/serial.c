/*
 * serial.c - Serial Port Driver (UART 16550 on COM1)
 *
 * Implements polling-mode serial output for kernel debugging.
 * This is the first subsystem initialized during boot, providing
 * debug output before the framebuffer or any other driver is ready.
 *
 * The driver uses busy-waiting (polling) rather than interrupts.
 * This is simpler and works reliably in any context, including
 * early boot and inside interrupt handlers.
 */

#include "serial.h"
#include "io.h"

/* =============================================================================
 * UART 16550 Hardware Constants
 * =============================================================================
 */

/* COM1 base I/O port address */
#define SERIAL_COM1_BASE     0x3F8

/* Register offsets from base address */
#define SERIAL_DATA          (SERIAL_COM1_BASE + 0)  /* Data / Divisor low (DLAB=1) */
#define SERIAL_INTR_ENABLE   (SERIAL_COM1_BASE + 1)  /* Interrupt enable / Divisor high (DLAB=1) */
#define SERIAL_FIFO_CTRL     (SERIAL_COM1_BASE + 2)  /* FIFO control register */
#define SERIAL_LINE_CTRL     (SERIAL_COM1_BASE + 3)  /* Line control register */
#define SERIAL_MODEM_CTRL    (SERIAL_COM1_BASE + 4)  /* Modem control register */
#define SERIAL_LINE_STATUS   (SERIAL_COM1_BASE + 5)  /* Line status register */

/* Line Control Register bits */
#define SERIAL_LCR_DLAB      0x80     /* Divisor Latch Access Bit - set to access divisor */
#define SERIAL_LCR_8N1       0x03     /* 8 data bits, No parity, 1 stop bit */

/* FIFO Control Register bits */
#define SERIAL_FCR_ENABLE    0xC7     /* Enable FIFO, clear buffers, 14-byte threshold */

/* Modem Control Register bits */
#define SERIAL_MCR_RTS_DTR   0x03     /* RTS and DTR signals active */

/* Baud rate divisor: 115200 / desired_baud. For 38400 baud: 115200/38400 = 3 */
#define SERIAL_DIVISOR_38400 0x03

/* Line Status Register bits */
#define SERIAL_LSR_DATA_READY 0x01  /* Data available in receive buffer */
#define SERIAL_LSR_TX_EMPTY   0x20  /* Transmitter empty, ready to send */

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

/*
 * serial_is_transmit_ready - Check if the UART is ready to accept data.
 *
 * Returns non-zero if the transmit buffer is empty and we can write.
 */
static int serial_is_transmit_ready(void) {
    return inb(SERIAL_LINE_STATUS) & SERIAL_LSR_TX_EMPTY;
}

/* =============================================================================
 * Public API Implementation
 * =============================================================================
 */

/*
 * serial_init - Initialize the serial port for 38400 baud, 8N1.
 *
 * The initialization sequence:
 *   1. Disable interrupts (we poll for simplicity)
 *   2. Set DLAB to access baud rate divisor
 *   3. Set divisor for desired baud rate
 *   4. Clear DLAB and set line format (8N1)
 *   5. Enable and clear FIFOs
 *   6. Set modem control lines
 */
void serial_init(void) {
    outb(SERIAL_INTR_ENABLE, 0x00);              /* Disable all UART interrupts */
    outb(SERIAL_LINE_CTRL,   SERIAL_LCR_DLAB);   /* Enable DLAB to set baud rate */
    outb(SERIAL_DATA,        SERIAL_DIVISOR_38400); /* Divisor low byte */
    outb(SERIAL_INTR_ENABLE, 0x00);              /* Divisor high byte (0 for 38400) */
    outb(SERIAL_LINE_CTRL,   SERIAL_LCR_8N1);    /* 8 data bits, no parity, 1 stop */
    outb(SERIAL_FIFO_CTRL,   SERIAL_FCR_ENABLE); /* Enable and clear FIFOs */
    outb(SERIAL_MODEM_CTRL,  SERIAL_MCR_RTS_DTR);/* Assert RTS and DTR */
}

/*
 * serial_putc - Write a single character to the serial port.
 *
 * Blocks until the transmitter is ready, then sends the character.
 * This is a polling implementation (no interrupts).
 */
void serial_putc(char c) {
    while(!serial_is_transmit_ready())
        ;  /* Busy-wait until transmitter is ready */
    outb(SERIAL_DATA, c);
}

/*
 * serial_puts - Write a null-terminated string to the serial port.
 *
 * Converts '\n' to '\r\n' because serial terminals expect CR+LF line endings.
 */
void serial_puts(const char *s) {
    while(*s) {
        if(*s == '\n')
            serial_putc('\r');  /* Carriage return before line feed */
        serial_putc(*s);
        s++;
    }
}

/*
 * serial_put_hex - Print a 64-bit value in hexadecimal.
 *
 * Always prints all 16 hex digits with "0x" prefix.
 * Used for displaying addresses, which should show full 64-bit values.
 *
 * Algorithm: Extract each 4-bit nibble from most-significant to least,
 * convert to hex digit, and output.
 */
#define HEX64_NUM_NIBBLES      16   /* Number of hex digits in 64-bit value */
#define HEX64_TOP_NIBBLE_SHIFT 60   /* Shift to get topmost nibble (15 * 4) */
#define BITS_PER_NIBBLE        4    /* Each hex digit represents 4 bits */

void serial_put_hex(uint64_t value) {
    const char *hex_digits = "0123456789abcdef";
    serial_puts("0x");
    for(int shift = HEX64_TOP_NIBBLE_SHIFT; shift >= 0; shift -= BITS_PER_NIBBLE) {
        serial_putc(hex_digits[(value >> shift) & 0xF]);
    }
}

/*
 * serial_put_dec - Print a 64-bit value in decimal.
 *
 * Algorithm: Extract digits from least-significant to most by repeated
 * division by 10, store in buffer, then output in reverse order.
 *
 * Buffer size: A 64-bit unsigned integer has at most 20 decimal digits
 * (2^64 - 1 = 18,446,744,073,709,551,615).
 */
#define MAX_UINT64_DECIMAL_DIGITS 21  /* 20 digits + null terminator */

void serial_put_dec(uint64_t value) {
    if(value == 0) {
        serial_putc('0');
        return;
    }
    char buf[MAX_UINT64_DECIMAL_DIGITS];
    int i = 0;
    while(value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    /* Output digits in reverse order (most significant first) */
    while(i > 0) {
        serial_putc(buf[--i]);
    }
}

/* =============================================================================
 * Serial Input Functions
 * =============================================================================
 */

/*
 * serial_has_char - Check if a character is available to read.
 *
 * Returns non-zero if data is available in the receive buffer.
 */
int serial_has_char(void) {
    return inb(SERIAL_LINE_STATUS) & SERIAL_LSR_DATA_READY;
}

/*
 * serial_getc - Read a single character from the serial port.
 *
 * Blocks until a character is available (polling mode).
 * Returns the received character.
 */
char serial_getc(void) {
    while(!serial_has_char())
        ;  /* Busy-wait until data is ready */
    return inb(SERIAL_DATA);
}

/*
 * serial_read - Read up to len characters from the serial port.
 *
 * Non-blocking: returns immediately with available characters.
 * Returns the number of characters actually read.
 */
size_t serial_read(char *buf, size_t len) {
    size_t bytes_read = 0;
    while(bytes_read < len && serial_has_char()) {
        buf[bytes_read++] = inb(SERIAL_DATA);
    }
    return bytes_read;
}

/*
 * serial_wait - Block until serial input is available.
 *
 * Uses HLT to reduce CPU usage while waiting.
 */
void serial_wait(void) {
    if(serial_has_char()) {
        return;
    }
    while(!serial_has_char()) {
        __asm__ volatile("sti; hlt; cli");
    }
    __asm__ volatile("sti");
}
