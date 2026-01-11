// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel Init Loader
 *
 * Loads and executes /init from the initrd filesystem.
 * This is the final step in kernel initialization - after this,
 * we're running in userspace.
 */

#include "kinit.h"
#include "process.h"
#include "elf.h"
#include "ext2.h"
#include "limine.h"
#include "vmm.h"
#include "gdt.h"
#include "percpu.h"
#include "heap.h"
#include "log.h"
#include "usermode.h"
#include "syscall_table.h"

/*
 * Helper: Copy memory
 */
static void kinit_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

/**
 * start_init - Load and execute /init from initrd
 *
 * This function:
 * 1. Gets the initrd module from Limine
 * 2. Initializes ext2 filesystem on it (already done in kmain)
 * 3. Looks up /init
 * 4. Creates init process (PID 1)
 * 5. Loads ELF into the process address space
 * 6. Sets up user stack
 * 7. Enters userspace
 *
 * This function does NOT return.
 */
void start_init(void)
{
    struct limine_file *initrd;
    ext2_inode_t *init_inode;
    uint32_t init_ino;
    void *elf_data;
    uint64_t file_size;
    ssize_t bytes_read;
    process_t *init;
    elf_info_t elf_info;
    uint64_t user_rsp;
    int ret;

    log_info("KINIT: Starting init process...");

    /*
     * Step 1: Verify initrd is available
     *
     * ext2 was already initialized in kmain(), so we just need
     * to look up the /init file.
     */
    initrd = limine_get_module(0);
    if (!initrd) {
        log_panic("KINIT: No initrd module found");
    }
    log_debug("KINIT: initrd at %p, size %llu bytes", initrd->address, initrd->size);

    /*
     * Step 2: Look up /init in ext2 filesystem
     */
    init_ino = ext2_lookup("/init");
    if (init_ino == 0) {
        log_panic("KINIT: /init not found in initrd");
    }
    log_debug("KINIT: /init found, inode %u", init_ino);

    /*
     * Step 3: Read /init inode and get file size
     */
    init_inode = ext2_read_inode(init_ino);
    if (!init_inode) {
        log_panic("KINIT: Failed to read /init inode");
    }

    file_size = ext2_get_file_size(init_inode);
    log_debug("KINIT: /init size: %llu bytes", file_size);

    if (file_size == 0) {
        log_panic("KINIT: /init is empty");
    }

    /*
     * Step 4: Allocate buffer and read ELF file
     */
    elf_data = kmalloc(file_size);
    if (!elf_data) {
        log_panic("KINIT: Failed to allocate %llu bytes for /init", file_size);
    }

    bytes_read = ext2_read_file(init_inode, 0, elf_data, file_size);
    if (bytes_read < 0 || (uint64_t)bytes_read != file_size) {
        kfree(elf_data);
        log_panic("KINIT: Failed to read /init (got %lld bytes, expected %llu)",
                  (long long)bytes_read, file_size);
    }
    log_debug("KINIT: Read %llu bytes of /init", file_size);

    /*
     * Step 5: Create init process (PID 1)
     */
    init = process_create("init");
    if (!init) {
        kfree(elf_data);
        log_panic("KINIT: Failed to create init process");
    }
    log_debug("KINIT: Created init process (PID %llu)", init->pid);

    /*
     * Step 6: Load ELF into process address space
     */
    ret = elf_load(init, elf_data, file_size, &elf_info);
    if (ret < 0) {
        kfree(elf_data);
        process_destroy(init);
        log_panic("KINIT: Failed to load /init ELF (error %d)", ret);
    }
    log_info("KINIT: Loaded /init, entry=0x%llx", elf_info.entry);

    /* Set brk values from ELF info */
    init->brk_start = elf_info.brk_start;
    init->brk = elf_info.brk_start;

    /* Done with ELF data in kernel heap */
    kfree(elf_data);

    /*
     * Step 7: Set up user stack with argc, argv, envp
     */
    char *argv[] = { "/init", NULL };
    char *envp[] = { "PATH=/bin", "HOME=/", NULL };

    user_rsp = elf_setup_stack(init, argv, envp, &elf_info);
    if (user_rsp == 0) {
        process_destroy(init);
        log_panic("KINIT: Failed to set up user stack");
    }
    log_debug("KINIT: User stack at 0x%llx", user_rsp);

    /*
     * Step 8: Prepare for userspace entry
     */

    /* Set init as current process */
    process_set_current(init);
    init->state = PROC_RUNNING;

    /* Update TSS.rsp0 for Ring 3 -> Ring 0 transitions */
    gdt_set_tss_rsp0(init->kernel_stack_top);

    /* Switch to init's address space */
    vmm_switch_address_space(init->pml4_phys);

    /* Save user context in process structure */
    init->user_rip = elf_info.entry;
    init->user_rsp = user_rsp;
    init->user_rflags = 0x202;  /* IF=1, reserved bit 1 set */

    /*
     * Step 9: Enter userspace!
     */
    log_info("KINIT: Entering userspace at 0x%llx, RSP=0x%llx", elf_info.entry, user_rsp);
    log_info("======== ENTERING RING 3 ========");

    user_mode_enter(elf_info.entry, user_rsp);

    /* Should never reach here */
    __builtin_unreachable();
}
