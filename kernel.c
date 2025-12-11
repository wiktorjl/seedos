/*
 * kernel.c - Kernel entry point and serial port driver
 *
 * This is the main kernel file containing:
 *   - kernel_main(): The entry point called by the bootloader
 *   - Serial port (UART 16550) driver for COM1 debug output
 *   - System initialization sequence
 *
 * The kernel boots via Limine bootloader which sets up:
 *   - Long mode (64-bit)
 *   - Higher-half mapping (kernel at 0xFFFFFFFF80000000)
 *   - HHDM (Higher Half Direct Map) for physical memory access
 *   - Framebuffer for graphics output
 *
 * Initialization order matters:
 *   1. Serial (for early debug output)
 *   2. Framebuffer + Console (for screen output)
 *   3. PMM (physical memory - needs memory map)
 *   4. GDT (segments for ring 0/3 transitions)
 *   5. VMM (virtual memory - needs PMM)
 *   6. IDT + PIC (interrupts)
 *   7. Keyboard (needs interrupts)
 *   8. Enter userspace, then start shell
 */

#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "io.h"
#include "pmm.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "shell.h"
#include "fb.h"
#include "gdt.h"
#include "console.h"
#include "vmm.h"
#include "tests.h"

/*
 * Limine bootloader requests.
 *
 * Limine scans the kernel binary for these magic structures and fills
 * in responses before jumping to kernel_main(). This is how we get:
 *   - HHDM offset (for physical memory access)
 *   - Memory map (for PMM initialization)
 *   - Framebuffer info (for graphics output)
 */
LIMINE_BASE_REVISION_DECLARATION;
LIMINE_HHDM_REQUEST;
LIMINE_MEMMAP_REQUEST;
LIMINE_FRAMEBUFFER_REQUEST;

/* =============================================================================
 * Serial Port Driver (UART 16550 on COM1)
 *
 * The serial port is essential for early kernel debugging. It works before
 * the framebuffer is initialized and output goes to the terminal running QEMU.
 *
 * UART 16550 registers (at base address 0x3F8 for COM1):
 *   +0: Data register (read/write data, or divisor low byte when DLAB=1)
 *   +1: Interrupt enable (or divisor high byte when DLAB=1)
 *   +2: FIFO control (write) / Interrupt ID (read)
 *   +3: Line control (data bits, parity, stop bits, DLAB)
 *   +4: Modem control (RTS, DTR, etc.)
 *   +5: Line status (transmit ready, receive ready, errors)
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

/* Line Status Register bit for "transmitter empty" */
#define SERIAL_LSR_TX_EMPTY  0x20

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
static void serial_init(void) {
    outb(SERIAL_INTR_ENABLE, 0x00);              /* Disable all UART interrupts */
    outb(SERIAL_LINE_CTRL,   SERIAL_LCR_DLAB);   /* Enable DLAB to set baud rate */
    outb(SERIAL_DATA,        SERIAL_DIVISOR_38400); /* Divisor low byte */
    outb(SERIAL_INTR_ENABLE, 0x00);              /* Divisor high byte (0 for 38400) */
    outb(SERIAL_LINE_CTRL,   SERIAL_LCR_8N1);    /* 8 data bits, no parity, 1 stop */
    outb(SERIAL_FIFO_CTRL,   SERIAL_FCR_ENABLE); /* Enable and clear FIFOs */
    outb(SERIAL_MODEM_CTRL,  SERIAL_MCR_RTS_DTR);/* Assert RTS and DTR */
}

/*
 * serial_is_transmit_ready - Check if the UART is ready to accept data.
 *
 * Returns non-zero if the transmit buffer is empty and we can write.
 */
static int serial_is_transmit_ready(void) {
    return inb(SERIAL_LINE_STATUS) & SERIAL_LSR_TX_EMPTY;
}

/*
 * serial_putc - Write a single character to the serial port.
 *
 * Blocks until the transmitter is ready, then sends the character.
 * This is a polling implementation (no interrupts).
 */
void serial_putc(char c) {
    while (!serial_is_transmit_ready())
        ;  /* Busy-wait until transmitter is ready */
    outb(SERIAL_DATA, c);
}

/*
 * serial_puts - Write a null-terminated string to the serial port.
 *
 * Converts '\n' to '\r\n' because serial terminals expect CR+LF line endings.
 * This function is non-static so console.c can use it for dual output.
 */
void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
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
    for (int shift = HEX64_TOP_NIBBLE_SHIFT; shift >= 0; shift -= BITS_PER_NIBBLE) {
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
    if (value == 0) {
        serial_putc('0');
        return;
    }
    char buf[MAX_UINT64_DECIMAL_DIGITS];
    int i = 0;
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    /* Output digits in reverse order (most significant first) */
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}



/* =============================================================================
 * Kernel Entry Point
 *
 * Called from boot.S after the bootloader has:
 *   - Set up long mode (64-bit)
 *   - Mapped the kernel to higher half
 *   - Set up a stack for us
 *
 * From here we initialize all kernel subsystems and start the shell.
 * =============================================================================
 */
void kernel_main(void) {
    /* Initialize serial port for early debug output */
    serial_init();

    /* Initialize framebuffer and console for dual output */
    if (fb_init((void *)fb_request.response) == 0) {
        fb_console_init();
        console_init();
    }

    /* Verify bootloader provided required information */
    if (hhdm_request.response == NULL) {
        puts("[FAIL] No HHDM from bootloader\n");
        goto halt;
    }

    if (memmap_request.response == NULL) {
        puts("[FAIL] No memory map from bootloader\n");
        goto halt;
    }

    struct limine_memmap_response *memmap = memmap_request.response;
    uint64_t hhdm_offset = hhdm_request.response->offset;

    /* Display boot banner */
    puts("\n");
    puts("  ╔═══════════════════════════════════════╗\n");
    puts("  ║            S E E D   O S              ║\n");
    puts("  ║         x86-64 Hobby Kernel           ║\n");
    puts("  ╚═══════════════════════════════════════╝\n");
    puts("\n");

    /* Initialize kernel subsystems */
    puts("  Initializing kernel subsystems...\n\n");

    /* Physical memory manager */
    puts("    [....] PMM");
    pmm_init(memmap, hhdm_offset);
    puts("\r    [ OK ] PMM: ");
    put_dec(pmm_get_free_pages() * 4 / 1024);
    puts(" MB free\n");

    /* GDT with kernel and user segments */
    puts("    [....] GDT");
    gdt_init();
    puts("\r    [ OK ] GDT: kernel/user segments + TSS\n");

    /* Virtual memory manager */
    puts("    [....] VMM");
    vmm_init(hhdm_offset);
    puts("\r    [ OK ] VMM: 4-level paging ready\n");

    /* Interrupt descriptor table */
    puts("    [....] IDT");
    idt_init();
    puts("\r    [ OK ] IDT: 48 handlers + syscall\n");

    /* Programmable interrupt controller */
    puts("    [....] PIC");
    pic_init();
    puts("\r    [ OK ] PIC: remapped to IRQ 32-47\n");

    /* PS/2 keyboard driver */
    puts("    [....] Keyboard");
    keyboard_init();
    puts("\r    [ OK ] Keyboard: PS/2 driver ready\n");

    /* Initialize test subsystem with bootloader info */
    tests_init(memmap, hhdm_offset);

    /* Enable interrupts */
    asm volatile ("sti");

    puts("\n  System ready.\n");

    /* Start the shell */
    shell_init();

    /* Main loop - feed keyboard input to shell */
    while (1) {
        if (keyboard_has_char()) {
            char c = keyboard_get_char();
            shell_input(c);
        }

        /* Halt until next interrupt (saves power) */
        asm volatile ("hlt");
    }

halt:
    while (1) {
        asm volatile ("hlt");
    }
}
