// SPDX-License-Identifier: GPL-2.0-only
/*
 * ELF64 Loader
 *
 * Loads statically-linked ELF64 executables into a process's address space.
 * Supports Linux x86-64 ABI.
 */

#include "process.h"
#include "elf.h"
#include "log.h"
#include "syscall_table.h"
#include "pmm.h"
#include "vmm.h"
#include "memory.h"

/*
 * Helper: Zero memory
 */
static void elf_memset(void *dst, uint8_t val, size_t n)
{
    uint8_t *d = dst;
    for (size_t i = 0; i < n; i++)
        d[i] = val;
}

/*
 * Helper: Copy memory
 */
static void elf_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

/*
 * Helper: Get string length
 */
static size_t elf_strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

/*
 * Align value up to a boundary
 */
#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(val, align) ((val) & ~((align) - 1))

/**
 * elf_validate - Check if data is a valid ELF64 x86-64 executable
 */
int elf_validate(const void *data, size_t size)
{
    if (!data || size < sizeof(Elf64_Ehdr)) {
        log_error("ELF: Invalid data pointer or size too small");
        return -EINVAL;
    }

    const Elf64_Ehdr *ehdr = data;

    /* Check magic */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        log_error("ELF: Invalid magic number");
        return -ENOEXEC;
    }

    /* Check class (must be 64-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        log_error("ELF: Not a 64-bit ELF (class=%d)", ehdr->e_ident[EI_CLASS]);
        return -ENOEXEC;
    }

    /* Check endianness (must be little-endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        log_error("ELF: Not little-endian");
        return -ENOEXEC;
    }

    /* Check type (must be executable) */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        log_error("ELF: Not an executable (type=%d)", ehdr->e_type);
        return -ENOEXEC;
    }

    /* Check machine (must be x86-64) */
    if (ehdr->e_machine != EM_X86_64) {
        log_error("ELF: Not x86-64 (machine=%d)", ehdr->e_machine);
        return -ENOEXEC;
    }

    /* Check program header size */
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
        log_error("ELF: Invalid program header size (%d != %zu)",
                  ehdr->e_phentsize, sizeof(Elf64_Phdr));
        return -ENOEXEC;
    }

    /* Check that program headers fit in file */
    if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf64_Phdr) > size) {
        log_error("ELF: Program headers extend beyond file");
        return -ENOEXEC;
    }

    return 0;
}

/**
 * elf_load - Load an ELF64 executable into a process's address space
 */
int elf_load(struct process *proc, const void *data, size_t size, elf_info_t *info)
{
    int ret;

    if (!proc || !data || !info)
        return -EINVAL;

    /* Validate ELF */
    ret = elf_validate(data, size);
    if (ret < 0)
        return ret;

    const Elf64_Ehdr *ehdr = data;
    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)((const uint8_t *)data + ehdr->e_phoff);

    log_debug("ELF: Loading %d program headers, entry=0x%llx",
              ehdr->e_phnum, ehdr->e_entry);

    info->entry = ehdr->e_entry;
    info->phnum = ehdr->e_phnum;
    info->phdr_addr = 0;  /* Will be set if we find PT_PHDR or first PT_LOAD */
    info->brk_start = 0;

    /* Process each program header */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = &phdrs[i];

        if (phdr->p_type != PT_LOAD)
            continue;

        /* Skip empty segments */
        if (phdr->p_memsz == 0)
            continue;

        log_debug("ELF: PT_LOAD[%d] vaddr=0x%llx filesz=%llu memsz=%llu flags=0x%x",
                  i, phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz, phdr->p_flags);

        /* Validate addresses are in user space */
        if (phdr->p_vaddr >= 0x800000000000ULL) {
            log_error("ELF: Segment at 0x%llx is in kernel space", phdr->p_vaddr);
            return -ENOEXEC;
        }

        /* Calculate page-aligned start and end */
        uint64_t seg_start = ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE);
        uint64_t seg_end = ALIGN_UP(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);
        uint64_t num_pages = (seg_end - seg_start) / PAGE_SIZE;

        /* Convert segment flags to page flags */
        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (phdr->p_flags & PF_W)
            pte_flags |= PTE_WRITABLE;
        if (!(phdr->p_flags & PF_X))
            pte_flags |= PTE_NX;

        /* Allocate and map pages */
        for (uint64_t page_idx = 0; page_idx < num_pages; page_idx++) {
            uint64_t vaddr = seg_start + page_idx * PAGE_SIZE;

            /* Check if already mapped (overlapping segments) */
            uint64_t existing = vmm_get_physical(proc->pml4_phys, vaddr);
            if (existing != 0)
                continue;  /* Already mapped */

            /* Allocate physical page */
            uint64_t phys = pmm_alloc();
            if (phys == PMM_ALLOC_FAILED) {
                log_error("ELF: Out of memory allocating page for 0x%llx", vaddr);
                return -ENOMEM;
            }

            /* Zero the page first */
            elf_memset(phys_to_virt(phys), 0, PAGE_SIZE);

            /* Map the page */
            ret = vmm_map_page(proc->pml4_phys, vaddr, phys, pte_flags);
            if (ret < 0) {
                log_error("ELF: Failed to map page at 0x%llx", vaddr);
                pmm_free(phys);
                return ret;
            }
        }

        /* Copy file data into segment */
        if (phdr->p_filesz > 0) {
            /* Check that file data is within bounds */
            if (phdr->p_offset + phdr->p_filesz > size) {
                log_error("ELF: Segment data extends beyond file");
                return -ENOEXEC;
            }

            const uint8_t *src = (const uint8_t *)data + phdr->p_offset;
            uint64_t remaining = phdr->p_filesz;
            uint64_t offset_in_seg = phdr->p_vaddr - seg_start;
            uint64_t dst_vaddr = phdr->p_vaddr;

            while (remaining > 0) {
                uint64_t page_start = ALIGN_DOWN(dst_vaddr, PAGE_SIZE);
                uint64_t page_offset = dst_vaddr - page_start;
                uint64_t to_copy = PAGE_SIZE - page_offset;
                if (to_copy > remaining)
                    to_copy = remaining;

                /* Get physical address and write through HHDM */
                uint64_t phys = vmm_get_physical(proc->pml4_phys, page_start);
                if (phys == 0) {
                    log_error("ELF: Page not mapped at 0x%llx", page_start);
                    return -EFAULT;
                }

                uint8_t *dst = (uint8_t *)phys_to_virt(phys) + page_offset;
                elf_memcpy(dst, src, to_copy);

                src += to_copy;
                dst_vaddr += to_copy;
                remaining -= to_copy;
            }
        }

        /* Track highest address for brk */
        uint64_t seg_high = phdr->p_vaddr + phdr->p_memsz;
        if (seg_high > info->brk_start)
            info->brk_start = seg_high;

        /* Use first PT_LOAD as phdr address hint (where phdrs might be mapped) */
        if (info->phdr_addr == 0 && phdr->p_offset == 0 &&
            phdr->p_filesz >= ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf64_Phdr)) {
            info->phdr_addr = phdr->p_vaddr + ehdr->e_phoff;
        }
    }

    /* Page-align brk_start */
    info->brk_start = ALIGN_UP(info->brk_start, PAGE_SIZE);

    log_info("ELF: Loaded, entry=0x%llx, brk=0x%llx", info->entry, info->brk_start);

    return 0;
}

/**
 * elf_setup_stack - Set up user stack with argc, argv, envp, auxv
 *
 * Stack layout (growing down from USER_STACK_TOP):
 *   [string data]
 *   [16 random bytes]
 *   [padding for alignment]
 *   [AT_NULL, 0]
 *   [auxv entries...]
 *   [NULL] (envp terminator)
 *   [envp pointers...]
 *   [NULL] (argv terminator)
 *   [argv pointers...]
 *   [argc] <- returned RSP
 */
uint64_t elf_setup_stack(struct process *proc, char **argv, char **envp,
                         const elf_info_t *info)
{
    int ret;

    if (!proc || !argv || !info)
        return 0;

    /* Allocate stack pages (64KB) */
    uint64_t stack_top = USER_STACK_TOP;
    uint64_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;

    for (uint64_t i = 0; i < stack_pages; i++) {
        uint64_t vaddr = stack_top - (i + 1) * PAGE_SIZE;
        uint64_t phys = pmm_alloc();
        if (phys == PMM_ALLOC_FAILED) {
            log_error("ELF: Out of memory for stack");
            return 0;
        }
        elf_memset(phys_to_virt(phys), 0, PAGE_SIZE);

        ret = vmm_map_page(proc->pml4_phys, vaddr,
                          phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
        if (ret < 0) {
            pmm_free(phys);
            return 0;
        }
    }

    /* Count arguments */
    int argc = 0;
    while (argv[argc])
        argc++;

    /* Count environment variables */
    int envc = 0;
    if (envp) {
        while (envp[envc])
            envc++;
    }

    /*
     * Calculate space needed on stack:
     * - String data for argv and envp
     * - 16 random bytes
     * - argv pointers + NULL
     * - envp pointers + NULL
     * - auxv (6 entries + AT_NULL)
     * - argc
     */

    /* Calculate total string length */
    size_t strings_size = 0;
    for (int i = 0; i < argc; i++)
        strings_size += elf_strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++)
        strings_size += elf_strlen(envp[i]) + 1;

    /* Build stack from top down */
    uint64_t sp = stack_top;

    /* Copy strings (at top of stack) */
    sp -= strings_size;
    sp = ALIGN_DOWN(sp, 8);
    uint64_t strings_base = sp;

    /* Copy string data via HHDM */
    uint64_t str_ptr = strings_base;
    uint64_t argv_ptrs[argc + 1];
    uint64_t envp_ptrs[envc + 1];

    for (int i = 0; i < argc; i++) {
        argv_ptrs[i] = str_ptr;
        size_t len = elf_strlen(argv[i]) + 1;

        /* Copy string byte by byte through page table */
        for (size_t j = 0; j < len; j++) {
            uint64_t page = ALIGN_DOWN(str_ptr + j, PAGE_SIZE);
            uint64_t phys = vmm_get_physical(proc->pml4_phys, page);
            if (phys == 0) {
                log_error("ELF: Stack page not mapped at 0x%llx", page);
                return 0;
            }
            uint8_t *dst = (uint8_t *)phys_to_virt(phys) + ((str_ptr + j) & (PAGE_SIZE - 1));
            *dst = argv[i][j];
        }
        str_ptr += len;
    }
    argv_ptrs[argc] = 0;  /* NULL terminator */

    for (int i = 0; i < envc; i++) {
        envp_ptrs[i] = str_ptr;
        size_t len = elf_strlen(envp[i]) + 1;

        for (size_t j = 0; j < len; j++) {
            uint64_t page = ALIGN_DOWN(str_ptr + j, PAGE_SIZE);
            uint64_t phys = vmm_get_physical(proc->pml4_phys, page);
            if (phys == 0)
                return 0;
            uint8_t *dst = (uint8_t *)phys_to_virt(phys) + ((str_ptr + j) & (PAGE_SIZE - 1));
            *dst = envp[i][j];
        }
        str_ptr += len;
    }
    envp_ptrs[envc] = 0;  /* NULL terminator */

    /* Random bytes for AT_RANDOM (16 bytes) */
    sp -= 16;
    sp = ALIGN_DOWN(sp, 16);
    uint64_t random_addr = sp;

    /* Write some pseudo-random bytes (just use addresses as "randomness") */
    uint64_t page = ALIGN_DOWN(random_addr, PAGE_SIZE);
    uint64_t phys = vmm_get_physical(proc->pml4_phys, page);
    if (phys) {
        uint8_t *rand = (uint8_t *)phys_to_virt(phys) + (random_addr & (PAGE_SIZE - 1));
        for (int i = 0; i < 16; i++)
            rand[i] = (uint8_t)((random_addr >> (i & 7)) ^ (i * 17));
    }

    /* Helper to write a uint64_t to user stack */
    #define PUSH_U64(val) do { \
        sp -= 8; \
        uint64_t _pg = ALIGN_DOWN(sp, PAGE_SIZE); \
        uint64_t _ph = vmm_get_physical(proc->pml4_phys, _pg); \
        if (_ph == 0) return 0; \
        *(uint64_t *)((uint8_t *)phys_to_virt(_ph) + (sp & (PAGE_SIZE - 1))) = (val); \
    } while (0)

    /* Align for auxv */
    sp = ALIGN_DOWN(sp, 16);

    /* Auxiliary vector (in reverse order since we push down) */
    PUSH_U64(0);           /* AT_NULL value */
    PUSH_U64(AT_NULL);     /* AT_NULL type */

    PUSH_U64(random_addr); /* AT_RANDOM value */
    PUSH_U64(AT_RANDOM);   /* AT_RANDOM type */

    PUSH_U64(info->entry); /* AT_ENTRY value */
    PUSH_U64(AT_ENTRY);    /* AT_ENTRY type */

    PUSH_U64(info->phnum); /* AT_PHNUM value */
    PUSH_U64(AT_PHNUM);    /* AT_PHNUM type */

    PUSH_U64(sizeof(Elf64_Phdr)); /* AT_PHENT value */
    PUSH_U64(AT_PHENT);           /* AT_PHENT type */

    if (info->phdr_addr) {
        PUSH_U64(info->phdr_addr); /* AT_PHDR value */
        PUSH_U64(AT_PHDR);         /* AT_PHDR type */
    }

    PUSH_U64(PAGE_SIZE);   /* AT_PAGESZ value */
    PUSH_U64(AT_PAGESZ);   /* AT_PAGESZ type */

    /* envp pointers (including NULL terminator) */
    for (int i = envc; i >= 0; i--)
        PUSH_U64(envp_ptrs[i]);

    /* argv pointers (including NULL terminator) */
    for (int i = argc; i >= 0; i--)
        PUSH_U64(argv_ptrs[i]);

    /* argc */
    PUSH_U64((uint64_t)argc);

    #undef PUSH_U64

    log_debug("ELF: Stack setup complete, RSP=0x%llx, argc=%d", sp, argc);

    return sp;
}
