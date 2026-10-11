/* ELF64 loader — static executables (ET_EXEC) for the running arch. */
#include <kernel.h>
#include <elf.h>

#ifdef __x86_64__
#define EM_THIS EM_X86_64
#else
#define EM_THIS EM_AARCH64
#endif

u64 elf_load(struct task *t, const void *image, size_t size, u64 *brk_out) {
    const Elf64_Ehdr *eh = image;
    if (size < sizeof(*eh) ||
        eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3)
        return 0;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_THIS) {
        early_printk("elf: bad type/machine (%u/%u)\n", eh->e_type, eh->e_machine);
        return 0;
    }

    u64 top = 0;
    for (u16 i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)((const u8 *)image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        u64 first = ALIGN_DOWN(ph->p_vaddr, PAGE_SIZE);
        u64 last  = ALIGN_UP(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        for (u64 va = first; va < last; va += PAGE_SIZE) {
            u64 pa = pmm_alloc_page();
            u64 flags = VMM_USER;
            if (ph->p_flags & PF_W) flags |= VMM_WRITE;
            if (ph->p_flags & PF_X) flags |= VMM_EXEC;
            vmm_map(task_root(t), va, pa, flags);
        }
        /* copy through the HHDM alias of the freshly mapped pages */
        u64 copied = 0;
        while (copied < ph->p_filesz) {
            u64 va = ph->p_vaddr + copied;
            u64 chunk = PAGE_SIZE - (va % PAGE_SIZE);
            if (chunk > ph->p_filesz - copied) chunk = ph->p_filesz - copied;
            /* translate va→pa by asking vmm (walk user root) */
            u64 pa = vmm_translate(task_root(t), va);
            if (!pa) break;
            memcpy(phys_to_virt(pa + (va % PAGE_SIZE)),
                   (const u8 *)image + ph->p_offset + copied, chunk);
            copied += chunk;
        }
        u64 seg_top = ph->p_vaddr + ph->p_memsz;
        if (seg_top > top) top = seg_top;
    }
    *brk_out = ALIGN_UP(top, PAGE_SIZE);
    return eh->e_entry;
}
