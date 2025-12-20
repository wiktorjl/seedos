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
#include "string.h"
#include "fb.h"
#include "gdt.h"
#include "console.h"
#include "pit.h"
#include "vfs.h"
#include "vmm.h"
#include "test_framework.h"
#include "test_all.h"
#include "tar.h"
#include "process.h"
#include "sched.h"

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
    if(hhdm_request.response == NULL) {
        puts("[error] HHDM request failed\n");
        return 0;
    }

    /* Check memory map response */
    if(memmap_request.response == NULL) {
        puts("[error] Memory map request failed\n");
        return 0;
    }

    /* Extract bootloader-provided information */
    *hhdm_offset = hhdm_request.response->offset;
    *memmap = memmap_request.response;

    return 1;
}

/* Get CPU model and name */
void print_cpu_info(void) {
    uint32_t eax, ebx, ecx, edx;
    char cpu_name[65] = {0};
    char vendor[13] = {0};
    char brand[49] = {0};
    char model[17] = {0};


    /* Get CPU vendor string */
    __asm__ volatile (
        "cpuid"
        : "=b"(ebx), "=d"(edx), "=c"(ecx)
        : "a"(0)
    );
    *((uint32_t *)&cpu_name[0])  = ebx;
    *((uint32_t *)&cpu_name[4])  = edx;
    *((uint32_t *)&cpu_name[8])  = ecx;

    /* Get CPU brand string */
    for(int i = 0; i < 3; i++) {
        __asm__ volatile (
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(0x80000002 + i)
        );
        *((uint32_t *)&cpu_name[16 + i * 16 + 0])  = eax;
        *((uint32_t *)&cpu_name[16 + i * 16 + 4])  = ebx;
        *((uint32_t *)&cpu_name[16 + i * 16 + 8])  = ecx;
        *((uint32_t *)&cpu_name[16 + i * 16 + 12]) = edx;
    }

    puts("[cpu] Name: ");
    puts(cpu_name);
    puts("\n");

    /* Extract vendor string */
    *((uint32_t *)&vendor[0])  = *((uint32_t *)&cpu_name[0]);
    *((uint32_t *)&vendor[4])  = *((uint32_t *)&cpu_name[4]);
    *((uint32_t *)&vendor[8])  = *((uint32_t *)&cpu_name[8]);
    vendor[12] = '\0';

    /* Extract brand string */
    strncpy(brand, &cpu_name[16], 48);
    brand[48] = '\0';

    /* Extract model string */
    strncpy(model, &cpu_name[16], 16);
    model[16] = '\0';

    puts("[cpu] Vendor: ");
    puts(vendor);
    puts("\n");

    puts("[cpu] Brand: ");
    puts(brand);
    puts("\n");

    puts("[cpu] Model: ");
    puts(model);
    puts("\n");
}

/* Print kernel banner */
void print_banner(void) {
    puts("\n");
    puts("SeedOS v0.1\n\n");
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
    if(fb_init((void *)fb_request.response) == 0) {
        fb_console_init();
        console_init();
    }else {
        puts("[warning] Framebuffer initialization failed\n");
    }
    
    if(!verify_bootloader_info(&memmap, &hhdm_offset)) {
        puts("Kernel panic: Invalid bootloader information\n");
        goto halt;
    }
    print_cpu_info();
    print_banner();

    /* Initialize kernel subsystems */
    pmm_init(memmap, hhdm_offset);
    puts("[pmm] Physical Memory Manager initialized\n");
    puts("[pmm] ");
    put_dec(pmm_get_usable_pages() * 4 / 1024);
    puts(" MB usable, ");
    put_dec(pmm_get_free_pages() * 4 / 1024);
    puts(" MB free\n");

    gdt_init();
    puts("[gdt] Kernel/user segments, TSS initialized\n");

    vmm_init(hhdm_offset);
    puts("[vmm] 4-level paging"); 
    puts("[vmm] HHDM @ ");
    put_hex(hhdm_offset);
    puts("\n");
    puts("[vmm] Page tables start at: ");
    put_hex(vmm_get_kernel_pml4());
    puts("\n");

    
    idt_init();

    puts("[idt] 48 handlers + syscall\n");

    pic_init();
    puts("[pic] IRQ 32-47\n");

    keyboard_init();
    puts("[keyboard] PS/2\n");

    pit_init();
    puts("[pit] 100 Hz timer\n");

    /* Initialize test framework and register all tests */
    test_framework_init(memmap, hhdm_offset);
    test_register_all();
    puts("[test] Registered all tests\n");
    puts("[test] Slots available: ");
    put_dec(MAX_TESTS);
    puts("\n");

    /* Enable interrupts */
    asm volatile ("sti");

    struct limine_module_response *mod = module_request.response;
    if(mod && mod->module_count > 0) {
        struct limine_file *initrd = mod->modules[0];
        tar_init(initrd->address, initrd->size);
        puts("[tarfs] tarfs loaded: ");
        put_dec(tar_get_file_count());
        puts(" files\n");
    }else {
        puts("[tarfs] [not found]\n");
    }

    vfs_init();
    puts("[vfs] tarfs mounted\n");
    /* Initialize scheduler */
    sched_init();
    puts("[sched] round-robin scheduler\n");

    /* Load and start /bin/init as the first userspace process */
    puts("[init] Starting init process...\n");
    {
        /* Find /bin/init in initrd (tarfs uses "bin/init" without leading slash) */
        struct tar_file *init_file = tar_find("bin/init");
        if(init_file == NULL) {
            puts("[error] /bin/init not found in initrd\n");
            goto halt;
        }

        /* Create process and load ELF */
        struct process *init = process_create();
        if(init == NULL) {
            puts("[error] Failed to create init process\n");
            goto halt;
        }

        if(process_load_elf(init, init_file->data, init_file->size) != 0) {
            puts("[error] Failed to load init ELF\n");
            process_destroy(init);
            goto halt;
        }

        puts("[init] PID ");
        put_dec(init->pid);
        puts("\n");

        /*
         * Enter userspace via blocking call. This properly builds the
         * iretq frame for kernel->user transition. Once init is running,
         * timer interrupts will have proper frames for preemptive scheduling.
         *
         * Init manages the system: runs shell, respawns on exit.
         * We only return here if init itself exits (system shutdown).
         */
        char *init_argv[] = { "init", 0 };
        int init_exit = process_run_with_args(init, 1, init_argv);
        puts("[init]     exited with code ");
        put_dec(init_exit);
        puts("\n");
        process_destroy(init);
    }

    puts("\nSystem halted.\n");

halt:
    while(1) {
        asm volatile ("hlt");
    }
}
