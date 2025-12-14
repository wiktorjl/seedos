/*
 * kernel.c - Kernel entry point and system initialization
 *
 * This is the main kernel file containing:
 *   - kernel_main(): The entry point called by the bootloader
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
 *   8. PIT (system timer)
 *   9. Start shell
 */

#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "serial.h"
#include "pmm.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "shell.h"
#include "fb.h"
#include "gdt.h"
#include "console.h"
#include "pit.h"
#include "vfs.h"
#include "vmm.h"
#include "test_framework.h"
#include "test_all.h"
#include "tar.h"

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
LIMINE_MODULE_REQUEST;

static struct limine_memmap_response *memmap;
static uint64_t hhdm_offset;

/* Forward declarations */
int verify_bootloader_info(struct limine_memmap_response **memmap, uint64_t *hhdm_offset) {
    /* Check HHDM response */
    if (hhdm_request.response == NULL) {
        puts("[error] HHDM request failed\n");
        return 0;
    }

    /* Check memory map response */
    if (memmap_request.response == NULL) {
        puts("[error] Memory map request failed\n");
        return 0;
    }

    /* Extract bootloader-provided information */
    *hhdm_offset = hhdm_request.response->offset;
    *memmap = memmap_request.response;

    return 1;
}

/* Print kernel banner */
void print_banner(void) {
    puts("\n");
    puts("  SeedOS v0.1\n");
    puts("  A Simple Educational Kernel\n");
    puts("\n");
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
    } else {
        puts("[warning] Framebuffer initialization failed\n");
    }
    
    if (!verify_bootloader_info(&memmap, &hhdm_offset)) {
        puts("Kernel panic: Invalid bootloader information\n");
        goto halt;
    }

    print_banner();

    /* Initialize kernel subsystems */
    pmm_init(memmap, hhdm_offset);
    puts("  pmm       ");
    put_dec(pmm_get_usable_pages() * 4 / 1024);
    puts(" MB usable, ");
    put_dec(pmm_get_free_pages() * 4 / 1024);
    puts(" MB free\n");

    gdt_init();
    puts("  gdt       kernel/user segments, tss\n");

    vmm_init(hhdm_offset);
    puts("  vmm       4-level paging, hhdm @ ");
    put_hex(hhdm_offset);
    puts("\n");

    idt_init();
    puts("  idt       48 handlers + syscall\n");

    pic_init();
    puts("  pic       irq 32-47\n");

    keyboard_init();
    puts("  keyboard  ps/2\n");

    pit_init();
    puts("  pit       100 Hz timer\n");

    /* Initialize test framework and register all tests */
    test_framework_init(memmap, hhdm_offset);
    test_register_all();
    puts("  tests     ");
    put_dec(MAX_TESTS);
    puts(" slots\n");

    asm volatile ("sti");

    struct limine_module_response *mod = module_request.response;
    if (mod && mod->module_count > 0) {
        struct limine_file *initrd = mod->modules[0];
        tar_init(initrd->address, initrd->size);
        puts("  initrd    ");
        put_dec(tar_get_file_count());
        puts(" files\n");
    } else {
        puts("  initrd    [not found]\n");
    }

    vfs_init();
    puts("  vfs       tarfs mounted\n");

    
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
