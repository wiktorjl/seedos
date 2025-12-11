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
#include "user_program.h"
#include "context.h"

/*
 * User program binary (assembled from user.S).
 * This is a simple "Hello World" program that runs in ring 3.
 * See user_program.c for the actual bytes.
 */
extern unsigned char user_bin[];
extern unsigned int user_bin_len;

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

/*
 * memmap_type_to_string - Convert Limine memory map type to human-readable string.
 *
 * The memory map from the bootloader describes regions of physical memory:
 *   - Usable: Free RAM we can allocate
 *   - Reserved: Used by hardware or firmware
 *   - ACPI Reclaimable: Can be reused after parsing ACPI tables
 *   - Kernel/Modules: Where our kernel code is loaded
 *   - Framebuffer: Video memory for graphics output
 */
static const char *memmap_type_to_string(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:                 return "Usable";
        case LIMINE_MEMMAP_RESERVED:               return "Reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI Reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS:               return "ACPI NVS";
        case LIMINE_MEMMAP_BAD_MEMORY:             return "Bad Memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader Reclaimable";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:     return "Kernel/Modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:            return "Framebuffer";
        default:                                   return "Unknown";
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
 * From here we initialize all kernel subsystems and eventually enter userspace.
 * =============================================================================
 */
void kernel_main(void) {
    serial_init();
    serial_puts("Hello from the kernel!\n\n");

    /*
     * Initialize framebuffer and console for dual output
     */
    if (fb_init((void *)fb_request.response) == 0) {
        fb_console_init();
        console_init();  /* Now puts() will go to both serial and FB */
        puts("Hello from the kernel!\n\n");
    }

    /* Check HHDM response */
    if (hhdm_request.response == NULL) {
        puts("ERROR: No HHDM from bootloader!\n");
        goto halt;
    }
    
    puts("HHDM offset: ");
    put_hex(hhdm_request.response->offset);
    puts("\n\n");

    /* Check if Limine gave us a memory map */
    if (memmap_request.response == NULL) {
        puts("ERROR: No memory map from bootloader!\n");
        goto halt;
    }

    struct limine_memmap_response *memmap = memmap_request.response;
    
    puts("Memory Map (");
    put_dec(memmap->entry_count);
    puts(" entries):\n");
    puts("----------------------------------------\n");

    uint64_t total_usable = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        puts("  ");
        put_hex(entry->base);
        puts(" - ");
        put_hex(entry->base + entry->length);
        puts(" (");
        put_dec(entry->length / 1024);
        puts(" KB) ");
        puts(memmap_type_to_string(entry->type));
        puts("\n");

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_usable += entry->length;
        }
    }

    puts("----------------------------------------\n");
    puts("Total usable memory: ");
    put_dec(total_usable / 1024 / 1024);
    puts(" MB\n\n");

    /*
     * Initialize the physical memory manager
     */
    puts("Initializing PMM...\n");
    pmm_init(memmap, hhdm_request.response->offset);
    
    puts("PMM initialized:\n");
    puts("  Total pages: ");
    put_dec(pmm_get_total_pages());
    puts("\n  Free pages:  ");
    put_dec(pmm_get_free_pages());
    puts(" (");
    put_dec(pmm_get_free_pages() * 4 / 1024);
    puts(" MB)\n\n");

    /*
     * Set up our own GDT with kernel and user segments
     */
    gdt_init();

    /*
     * Initialize virtual memory manager
     */
    vmm_init(hhdm_request.response->offset);

    /*
     * VMM Demo: Create a new address space and map some pages
     */
    puts("\nVMM Demo:\n");
    puts("----------------------------------------\n");

    /* Step 1: Create a new address space (for a future user process) */
    puts("  Creating new address space...\n");
    uint64_t user_pml4 = vmm_create_address_space();
    if (user_pml4 == 0) {
        puts("  ERROR: Failed to create address space!\n");
        goto halt;
    }
    puts("    New PML4 at physical: ");
    put_hex(user_pml4);
    puts("\n");

    /* Step 2: Allocate physical pages for user code and stack */
    puts("  Allocating physical pages...\n");
    uint64_t code_phys = pmm_alloc();
    uint64_t stack_phys = pmm_alloc();
    if (code_phys == 0 || stack_phys == 0) {
        puts("  ERROR: Failed to allocate pages!\n");
        goto halt;
    }
    puts("    Code page (physical):  ");
    put_hex(code_phys);
    puts("\n");
    puts("    Stack page (physical): ");
    put_hex(stack_phys);
    puts("\n");

    /* Step 3: Map user code at 0x400000 */
    puts("  Mapping user code: ");
    put_hex(USER_CODE_BASE);
    puts(" -> ");
    put_hex(code_phys);
    puts("\n");
    if (vmm_map_page(user_pml4, USER_CODE_BASE, code_phys,
                     PTE_PRESENT | PTE_USER) != 0) {
        puts("  ERROR: Failed to map code page!\n");
        goto halt;
    }

    /* Step 4: Map user stack at 0x7FFFFF000 (writable) */
    puts("  Mapping user stack: ");
    put_hex(USER_STACK_BASE);
    puts(" -> ");
    put_hex(stack_phys);
    puts("\n");
    if (vmm_map_page(user_pml4, USER_STACK_BASE, stack_phys,
                     PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
        puts("  ERROR: Failed to map stack page!\n");
        goto halt;
    }

    /* Step 5: Write test data to the physical pages via HHDM */
    puts("  Writing test pattern to code page via HHDM...\n");
    uint8_t *code_ptr = (uint8_t *)(code_phys + hhdm_request.response->offset);
    code_ptr[0] = 0xEB;  /* x86: JMP short (infinite loop) */
    code_ptr[1] = 0xFE;  /* offset -2 (jump to self) */
    puts("    Wrote 0xEB 0xFE (infinite loop) at code_ptr[0..1]\n");

    puts("  Writing test pattern to stack page via HHDM...\n");
    uint64_t *stack_ptr = (uint64_t *)(stack_phys + hhdm_request.response->offset);
    stack_ptr[0] = 0xDEADBEEFCAFEBABE;
    puts("    Wrote 0xDEADBEEFCAFEBABE at stack base\n");

    /* Step 6: Switch to new address space and verify kernel still works */
    puts("  Switching to new address space...\n");
    vmm_switch_address_space(user_pml4);
    puts("    SUCCESS! Kernel still accessible (upper half mapped)\n");

    /* Step 7: Switch back to kernel's original address space */
    puts("  Switching back to kernel address space...\n");
    vmm_switch_address_space(vmm_get_kernel_pml4());
    puts("    Back to kernel PML4\n");

    puts("----------------------------------------\n");
    puts("VMM Demo complete! Address space ready for userspace.\n");
    puts("  User code entry: ");
    put_hex(USER_CODE_BASE);
    puts("\n");
    puts("  User stack top:  ");
    put_hex(USER_STACK_BASE + 0x1000);
    puts("\n\n");

    /*
     * Initialize interrupt handling
     */
    puts("Initializing IDT...\n");
    idt_init();
    
    puts("Initializing PIC...\n");
    pic_init();
    
    puts("Initializing keyboard...\n");
    keyboard_init();
    
    /* Enable interrupts! */
    puts("Enabling interrupts...\n");
    asm volatile ("sti");
    
    /* Copy user program to code page */
    uint8_t *code_dest = (uint8_t *)(code_phys + hhdm_request.response->offset);
    for (size_t i = 0; i < user_bin_len; i++) {
        code_dest[i] = user_bin[i];
    }

    /*
     * Enter userspace!
     * When user calls sys_exit, we return here.
     */
    struct user_context ctx = {
        .pml4  = user_pml4,
        .entry = USER_CODE_BASE,
        .stack = USER_STACK_BASE + 0x1000
    };

    context_switch_to_user(&ctx);

    puts("\n========================================\n");
    puts("  Back from userspace!\n");
    puts("========================================\n\n");

    /* Start the shell */
    shell_init();

    /* Main loop - feed keyboard input to shell */
    while (1) {
        if (keyboard_has_char()) {
            char c = keyboard_get_char();
            shell_input(c);
        }
        
        /* Halt until next interrupt */
        asm volatile ("hlt");
    }

halt:
    /* Halt - we have nothing else to do yet */
    while (1) {
        asm volatile ("hlt");
    }
}
