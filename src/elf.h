/*
 * elf.h - ELF64 Binary Format Definitions
 *
 * ELF (Executable and Linkable Format) is the standard binary format
 * for executables on Unix-like systems. This header defines the
 * structures needed to parse and load ELF64 binaries.
 *
 * ELF File Structure:
 *
 *   ┌─────────────────────┐
 *   │     ELF Header      │  Identifies file, points to program headers
 *   ├─────────────────────┤
 *   │  Program Headers    │  Describe segments to load into memory
 *   ├─────────────────────┤
 *   │                     │
 *   │      Segments       │  Actual code and data
 *   │    (PT_LOAD, etc)   │
 *   │                     │
 *   ├─────────────────────┤
 *   │  Section Headers    │  Optional, for debugging/linking
 *   └─────────────────────┘
 *
 * For execution, we only need the ELF header and program headers.
 * Section headers are used by linkers/debuggers but not needed to run.
 */

#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* =============================================================================
 * ELF Identification (e_ident)
 * =============================================================================
 */

#define EI_NIDENT 16          /* Size of e_ident array */

/* e_ident indices */
#define EI_MAG0    0          /* File identification */
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4          /* File class (32/64 bit) */
#define EI_DATA    5          /* Data encoding (endianness) */
#define EI_VERSION 6          /* ELF version */
#define EI_OSABI   7          /* OS/ABI identification */

/* Magic number */
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

/* File class */
#define ELFCLASS32 1          /* 32-bit */
#define ELFCLASS64 2          /* 64-bit */

/* Data encoding */
#define ELFDATA2LSB 1         /* Little-endian */
#define ELFDATA2MSB 2         /* Big-endian */

/* =============================================================================
 * ELF Header
 * =============================================================================
 */

/* e_type values */
#define ET_NONE   0           /* No file type */
#define ET_REL    1           /* Relocatable file */
#define ET_EXEC   2           /* Executable file */
#define ET_DYN    3           /* Shared object file */
#define ET_CORE   4           /* Core file */

/* e_machine values */
#define EM_X86_64 62          /* AMD x86-64 */

/*
 * Elf64_Ehdr - ELF64 Header
 *
 * Located at the very beginning of the file.
 * Contains file identification and pointers to other headers.
 */
typedef struct {
    unsigned char e_ident[EI_NIDENT];  /* Magic number and other info */
    uint16_t e_type;                   /* Object file type */
    uint16_t e_machine;                /* Architecture */
    uint32_t e_version;                /* Object file version */
    uint64_t e_entry;                  /* Entry point virtual address */
    uint64_t e_phoff;                  /* Program header table file offset */
    uint64_t e_shoff;                  /* Section header table file offset */
    uint32_t e_flags;                  /* Processor-specific flags */
    uint16_t e_ehsize;                 /* ELF header size */
    uint16_t e_phentsize;              /* Program header table entry size */
    uint16_t e_phnum;                  /* Program header table entry count */
    uint16_t e_shentsize;              /* Section header table entry size */
    uint16_t e_shnum;                  /* Section header table entry count */
    uint16_t e_shstrndx;               /* Section name string table index */
} Elf64_Ehdr;

/* =============================================================================
 * Program Header
 * =============================================================================
 */

/* p_type values */
#define PT_NULL    0          /* Unused entry */
#define PT_LOAD    1          /* Loadable segment */
#define PT_DYNAMIC 2          /* Dynamic linking info */
#define PT_INTERP  3          /* Interpreter path */
#define PT_NOTE    4          /* Auxiliary information */
#define PT_PHDR    6          /* Program header table */

/* p_flags values */
#define PF_X 0x1              /* Execute permission */
#define PF_W 0x2              /* Write permission */
#define PF_R 0x4              /* Read permission */

/*
 * Elf64_Phdr - ELF64 Program Header
 *
 * Describes a segment to be loaded into memory.
 * PT_LOAD segments contain the actual code and data.
 */
typedef struct {
    uint32_t p_type;          /* Segment type */
    uint32_t p_flags;         /* Segment flags (permissions) */
    uint64_t p_offset;        /* Segment file offset */
    uint64_t p_vaddr;         /* Segment virtual address */
    uint64_t p_paddr;         /* Segment physical address (unused) */
    uint64_t p_filesz;        /* Segment size in file */
    uint64_t p_memsz;         /* Segment size in memory */
    uint64_t p_align;         /* Segment alignment */
} Elf64_Phdr;

/* =============================================================================
 * ELF Loader API
 * =============================================================================
 */

/*
 * elf_validate - Check if data is a valid ELF64 executable.
 *
 * @data: Pointer to the ELF file data
 * @size: Size of the data in bytes
 *
 * Returns: 0 if valid, -1 if invalid
 *
 * Checks:
 *   - Magic number (0x7f 'E' 'L' 'F')
 *   - 64-bit class
 *   - Little-endian encoding
 *   - Executable type
 *   - x86-64 architecture
 */
int elf_validate(const void *data, uint64_t size);

/*
 * elf_get_entry - Get the entry point from an ELF header.
 *
 * @data: Pointer to the ELF file data (must be validated first)
 *
 * Returns: Entry point virtual address
 */
uint64_t elf_get_entry(const void *data);

/*
 * elf_load - Load ELF segments into a process address space.
 *
 * @data:     Pointer to the ELF file data
 * @size:     Size of the data in bytes
 * @pml4:     Physical address of the process's PML4
 * @entry_out: Pointer to store the entry point address
 *
 * Returns: 0 on success, -1 on failure
 *
 * For each PT_LOAD segment:
 *   1. Allocates physical pages
 *   2. Maps them at the segment's virtual address
 *   3. Copies segment data from the file
 *   4. Zeros any BSS portion (p_memsz > p_filesz)
 */
int elf_load(const void *data, uint64_t size, uint64_t pml4, uint64_t *entry_out);

#endif /* ELF_H */
