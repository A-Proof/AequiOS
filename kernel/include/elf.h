/* Minimal ELF64 definitions for the AequiOS loader. */
#ifndef AEQ_ELF_H
#define AEQ_ELF_H

#include <kernel.h>

#define EI_NIDENT 16

typedef struct {
    u8  e_ident[EI_NIDENT];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} Elf64_Phdr;

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ET_EXEC 2
#define ET_DYN  3

#define EM_X86_64  62
#define EM_AARCH64 183

#define PT_LOAD   1
#define PT_INTERP 3

#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Load a static ELF64 image into task's user address space.
 * Returns entry VA, or 0 on failure. Sets *brk_out just past the image. */
u64 elf_load(struct task *t, const void *image, size_t size, u64 *brk_out);

#endif
