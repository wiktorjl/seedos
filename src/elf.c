/*
 * elf.c - ELF64 Loader Implementation
 *
 * Parses and loads ELF64 executables into a process address space.
 */

#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "memory.h"
#include "string.h"
#include <stddef.h>

int elf_validate(const void *data, uint64_t size) {
    /* Need at least the ELF header */
    if (size < sizeof(Elf64_Ehdr)) {
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    /* Check magic number */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return -1;
    }

    /* Check 64-bit */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return -1;
    }

    /* Check little-endian */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return -1;
    }

    /* Check executable type */
    if (ehdr->e_type != ET_EXEC) {
        return -1;
    }

    /* Check x86-64 architecture */
    if (ehdr->e_machine != EM_X86_64) {
        return -1;
    }

    /* Check program headers exist */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        return -1;
    }

    /* Check program headers are within file */
    uint64_t ph_end = ehdr->e_phoff + (ehdr->e_phnum * ehdr->e_phentsize);
    if (ph_end > size) {
        return -1;
    }

    return 0;
}

uint64_t elf_get_entry(const void *data) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
    return ehdr->e_entry;
}

/*
 * load_segment - Load a single PT_LOAD segment.
 *
 * Maps pages and copies data for one segment.
 */
static int load_segment(const Elf64_Phdr *ph, const uint8_t *file_base,
                        uint64_t pml4) {
    /* Determine page flags */
    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (ph->p_flags & PF_W) {
        flags |= PTE_WRITABLE;
    }

    /* Calculate page-aligned bounds */
    uint64_t seg_start = ph->p_vaddr;
    uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
    uint64_t page_start = seg_start & ~0xFFFULL;
    uint64_t page_end = (seg_end + 0xFFF) & ~0xFFFULL;

    /* Source data in file */
    const uint8_t *src = file_base + ph->p_offset;
    uint64_t file_size = ph->p_filesz;

    /* Map and populate each page */
    for (uint64_t vaddr = page_start; vaddr < page_end; vaddr += VMM_PAGE_SIZE) {
        /* Allocate physical page */
        uint64_t phys = pmm_alloc();
        if (phys == 0) {
            return -1;
        }

        /* Get kernel-accessible pointer to the page */
        uint8_t *page_ptr = (uint8_t *)phys_to_virt(phys);

        /* Zero the page first (handles BSS and partial pages) */
        memset(page_ptr, 0, VMM_PAGE_SIZE);

        /* Calculate what portion of this page overlaps with file data */
        uint64_t page_vaddr_start = vaddr;
        uint64_t page_vaddr_end = vaddr + VMM_PAGE_SIZE;

        /* Segment data bounds */
        uint64_t data_start = seg_start;
        uint64_t data_end = seg_start + file_size;

        /* Find overlap between page and file data */
        if (page_vaddr_end > data_start && page_vaddr_start < data_end) {
            /* There is overlap - calculate copy bounds */
            uint64_t copy_start = (page_vaddr_start > data_start) ?
                                   page_vaddr_start : data_start;
            uint64_t copy_end = (page_vaddr_end < data_end) ?
                                 page_vaddr_end : data_end;
            uint64_t copy_len = copy_end - copy_start;

            /* Offset within the page */
            uint64_t page_offset = copy_start - page_vaddr_start;

            /* Offset within file data */
            uint64_t file_offset = copy_start - seg_start;

            /* Copy the data */
            memcpy(page_ptr + page_offset, src + file_offset, copy_len);
        }

        /* Map the page into the process address space */
        if (vmm_map_page(pml4, vaddr, phys, flags) != 0) {
            pmm_free(phys);
            return -1;
        }
    }

    return 0;
}

int elf_load(const void *data, uint64_t size, uint64_t pml4, uint64_t *entry_out) {
    /* Validate first */
    if (elf_validate(data, size) != 0) {
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
    const uint8_t *file_base = (const uint8_t *)data;

    /* Get program headers */
    const Elf64_Phdr *phdr = (const Elf64_Phdr *)(file_base + ehdr->e_phoff);

    /* Process each program header */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *ph = &phdr[i];

        /* Only load PT_LOAD segments */
        if (ph->p_type != PT_LOAD) {
            continue;
        }

        /* Skip empty segments */
        if (ph->p_memsz == 0) {
            continue;
        }

        /* Validate segment is within file */
        if (ph->p_filesz > 0 && ph->p_offset + ph->p_filesz > size) {
            return -1;
        }

        /* Load this segment */
        if (load_segment(ph, file_base, pml4) != 0) {
            return -1;
        }
    }

    /* Return entry point */
    *entry_out = ehdr->e_entry;
    return 0;
}
