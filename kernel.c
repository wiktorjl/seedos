/*
 * kernel.c - Kernel main and serial output
 */

#include <stdint.h>
#include <stddef.h>
#include "limine.h"
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

extern unsigned char user_bin[];
extern unsigned int user_bin_len;

/* Limine requests - Limine scans for these and fills in responses */
LIMINE_BASE_REVISION_DECLARATION;
LIMINE_HHDM_REQUEST;
LIMINE_MEMMAP_REQUEST;
LIMINE_FRAMEBUFFER_REQUEST;

/*
 * I/O port access
 *
 * x86 has a separate I/O address space (not memory-mapped).
 * The 'out' and 'in' instructions access it.
 */
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile (
        "outb %0, %1"
        :                        /* no outputs */
        : "a"(value), "d"(port)  /* value in AL, port in DX */
    );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile (
        "inb %1, %0"
        : "=a"(value)   /* output in AL */
        : "d"(port)     /* port in DX */
    );
    return value;
}

/*
 * Serial port (UART 16550) on COM1
 */
#define SERIAL_PORT 0x3F8

#define SERIAL_DATA          (SERIAL_PORT + 0)  /* Data register */
#define SERIAL_INTR_ENABLE   (SERIAL_PORT + 1)  /* Interrupt enable */
#define SERIAL_FIFO_CTRL     (SERIAL_PORT + 2)  /* FIFO control */
#define SERIAL_LINE_CTRL     (SERIAL_PORT + 3)  /* Line control */
#define SERIAL_MODEM_CTRL    (SERIAL_PORT + 4)  /* Modem control */
#define SERIAL_LINE_STATUS   (SERIAL_PORT + 5)  /* Line status */

static void serial_init(void) {
    outb(SERIAL_INTR_ENABLE, 0x00);   /* Disable interrupts */
    outb(SERIAL_LINE_CTRL,   0x80);   /* Enable DLAB (divisor latch access) */
    outb(SERIAL_DATA,        0x03);   /* Divisor low byte: 38400 baud */
    outb(SERIAL_INTR_ENABLE, 0x00);   /* Divisor high byte */
    outb(SERIAL_LINE_CTRL,   0x03);   /* 8 bits, no parity, one stop bit */
    outb(SERIAL_FIFO_CTRL,   0xC7);   /* Enable FIFO, clear, 14-byte threshold */
    outb(SERIAL_MODEM_CTRL,  0x03);   /* RTS/DSR set */
}

static int serial_transmit_ready(void) {
    return inb(SERIAL_LINE_STATUS) & 0x20;  /* Bit 5 = transmit buffer empty */
}

void serial_putc(char c) {
    while (!serial_transmit_ready())
        ;  /* Wait until ready */
    outb(SERIAL_DATA, c);
}

/* Non-static so other modules can use them */
void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');  /* Serial terminals expect CR+LF */
        serial_putc(*s);
        s++;
    }
}

/* Simple hex printing for addresses */
void serial_put_hex(uint64_t value) {
    const char *hex = "0123456789abcdef";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(value >> i) & 0xF]);
    }
}

/* Simple decimal printing */
void serial_put_dec(uint64_t value) {
    if (value == 0) {
        serial_putc('0');
        return;
    }
    char buf[21];  /* Max 20 digits for 64-bit number + null */
    int i = 0;
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

static const char *memmap_type_str(uint64_t type) {
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


/*
 * Kernel entry point
 * Called from boot.S after stack is set up
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
        puts(memmap_type_str(entry->type));
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
