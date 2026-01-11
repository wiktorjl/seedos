// SPDX-License-Identifier: GPL-2.0-only
/*
 * ELF64 Loader
 *
 * Structures and functions for loading ELF64 executables.
 * Supports static executables with Linux x86-64 ABI.
 */

#ifndef _KERNEL_ELF_H
#define _KERNEL_ELF_H

#include <stdint.h>
#include <stddef.h>

struct process;  /* Forward declaration */

/*
 * ELF64 Header
 */
typedef struct {
    uint8_t  e_ident[16];     /* Magic: 0x7f 'E' 'L' 'F', class, endian, etc. */
    uint16_t e_type;          /* Object type */
    uint16_t e_machine;       /* Machine type */
    uint32_t e_version;       /* Object file version */
    uint64_t e_entry;         /* Entry point address */
    uint64_t e_phoff;         /* Program header offset */
    uint64_t e_shoff;         /* Section header offset */
    uint32_t e_flags;         /* Processor-specific flags */
    uint16_t e_ehsize;        /* ELF header size */
    uint16_t e_phentsize;     /* Program header entry size */
    uint16_t e_phnum;         /* Number of program headers */
    uint16_t e_shentsize;     /* Section header entry size */
    uint16_t e_shnum;         /* Number of section headers */
    uint16_t e_shstrndx;      /* Section name string table index */
} __attribute__((packed)) Elf64_Ehdr;

/*
 * ELF64 Program Header
 */
typedef struct {
    uint32_t p_type;          /* Segment type */
    uint32_t p_flags;         /* Segment flags (PF_X, PF_W, PF_R) */
    uint64_t p_offset;        /* File offset */
    uint64_t p_vaddr;         /* Virtual address */
    uint64_t p_paddr;         /* Physical address (unused) */
    uint64_t p_filesz;        /* Size in file */
    uint64_t p_memsz;         /* Size in memory (>= filesz, diff is BSS) */
    uint64_t p_align;         /* Alignment */
} __attribute__((packed)) Elf64_Phdr;

/*
 * e_ident indices
 */
#define EI_MAG0       0       /* Magic byte 0 */
#define EI_MAG1       1       /* Magic byte 1 */
#define EI_MAG2       2       /* Magic byte 2 */
#define EI_MAG3       3       /* Magic byte 3 */
#define EI_CLASS      4       /* File class */
#define EI_DATA       5       /* Data encoding */
#define EI_VERSION    6       /* File version */
#define EI_OSABI      7       /* OS/ABI identification */
#define EI_ABIVERSION 8       /* ABI version */

/*
 * Magic values
 */
#define ELFMAG0       0x7f
#define ELFMAG1       'E'
#define ELFMAG2       'L'
#define ELFMAG3       'F'

/*
 * e_ident[EI_CLASS] values
 */
#define ELFCLASS32    1       /* 32-bit objects */
#define ELFCLASS64    2       /* 64-bit objects */

/*
 * e_ident[EI_DATA] values
 */
#define ELFDATA2LSB   1       /* Little-endian */
#define ELFDATA2MSB   2       /* Big-endian */

/*
 * e_type values
 */
#define ET_NONE       0       /* No file type */
#define ET_REL        1       /* Relocatable file */
#define ET_EXEC       2       /* Executable file */
#define ET_DYN        3       /* Shared object file */
#define ET_CORE       4       /* Core file */

/*
 * e_machine values
 */
#define EM_386        3       /* Intel 80386 */
#define EM_X86_64     62      /* AMD x86-64 */

/*
 * p_type values
 */
#define PT_NULL       0       /* Unused entry */
#define PT_LOAD       1       /* Loadable segment */
#define PT_DYNAMIC    2       /* Dynamic linking info */
#define PT_INTERP     3       /* Interpreter path */
#define PT_NOTE       4       /* Auxiliary info */
#define PT_SHLIB      5       /* Reserved */
#define PT_PHDR       6       /* Program header table */
#define PT_TLS        7       /* Thread-local storage */
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK  0x6474e551
#define PT_GNU_RELRO  0x6474e552

/*
 * p_flags values
 */
#define PF_X          0x1     /* Executable */
#define PF_W          0x2     /* Writable */
#define PF_R          0x4     /* Readable */

/*
 * Auxiliary vector types (for stack setup)
 */
#define AT_NULL       0       /* End of vector */
#define AT_IGNORE     1       /* Ignore entry */
#define AT_EXECFD     2       /* File descriptor of program */
#define AT_PHDR       3       /* Program header address */
#define AT_PHENT      4       /* Program header entry size */
#define AT_PHNUM      5       /* Number of program headers */
#define AT_PAGESZ     6       /* Page size */
#define AT_BASE       7       /* Interpreter base address */
#define AT_FLAGS      8       /* Flags */
#define AT_ENTRY      9       /* Entry point */
#define AT_NOTELF     10      /* Program is not ELF */
#define AT_UID        11      /* Real UID */
#define AT_EUID       12      /* Effective UID */
#define AT_GID        13      /* Real GID */
#define AT_EGID       14      /* Effective GID */
#define AT_PLATFORM   15      /* Platform string */
#define AT_HWCAP      16      /* Hardware capabilities */
#define AT_CLKTCK     17      /* Clock ticks per second */
#define AT_SECURE     23      /* Secure mode boolean */
#define AT_RANDOM     25      /* Address of 16 random bytes */
#define AT_EXECFN     31      /* Filename of program */

/*
 * User stack configuration
 * Note: USER_STACK_TOP and USER_STACK_SIZE are defined in process.h
 */

/*
 * ELF loading result - info needed for stack setup
 */
typedef struct {
    uint64_t entry;           /* Entry point */
    uint64_t phdr_addr;       /* Address where phdrs are loaded */
    uint16_t phnum;           /* Number of program headers */
    uint64_t brk_start;       /* End of last segment (for brk) */
} elf_info_t;

/**
 * elf_load - Load an ELF64 executable into a process's address space
 * @proc: Target process
 * @data: Pointer to ELF file data
 * @size: Size of ELF file
 * @info: Output structure for entry point and phdr info
 *
 * Returns 0 on success, negative error code on failure.
 */
int elf_load(struct process *proc, const void *data, size_t size, elf_info_t *info);

/**
 * elf_setup_stack - Set up user stack with argc, argv, envp, auxv
 * @proc: Target process
 * @argv: Argument strings (NULL-terminated array)
 * @envp: Environment strings (NULL-terminated array)
 * @info: ELF info from elf_load()
 *
 * Returns user RSP on success, 0 on failure.
 */
uint64_t elf_setup_stack(struct process *proc, char **argv, char **envp,
                         const elf_info_t *info);

/**
 * elf_validate - Check if data is a valid ELF64 x86-64 executable
 * @data: Pointer to potential ELF data
 * @size: Size of data
 *
 * Returns 0 if valid, negative error code otherwise.
 */
int elf_validate(const void *data, size_t size);

#endif /* _KERNEL_ELF_H */
