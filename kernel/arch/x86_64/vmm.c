/* x86_64 — paging.
 * Kernel keeps Limine's tables. A user task gets a private PML4 whose
 * high half is a copy of the kernel's (kernel stays mapped supervisor
 * under the user CR3 — syscall entry needs no CR3 switch). */
#include <kernel.h>

#define PT_PRESENT (1ULL << 0)
#define PT_WRITE   (1ULL << 1)
#define PT_USER    (1ULL << 2)
#define PT_PS      (1ULL << 7)
#define PT_ADDR    0x000FFFFFFFFFF000ULL

static u64 kernel_root;

static inline u64 *table(u64 phys) { return phys_to_virt(phys); }

void vmm_init(void) {
    u64 cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    kernel_root = cr3 & PT_ADDR;
    early_printk("vmm: kernel root %p\n", (void *)kernel_root);
}

static u64 *walk(u64 root, u64 va, bool create, int level) {
    u64 *t = table(root);
    for (int l = 3; l > level; l--) {
        int idx = (va >> (12 + l * 9)) & 0x1FF;
        u64 e = t[idx];
        if (!(e & PT_PRESENT)) {
            if (!create) return NULL;
            u64 nt = pmm_alloc_page();
            t[idx] = nt | PT_PRESENT | PT_WRITE | PT_USER;
            e = t[idx];
        }
        t = table(e & PT_ADDR);
    }
    return &t[(va >> (12 + level * 9)) & 0x1FF];
}

void vmm_map(u64 root, u64 va, u64 pa, u64 flags) {
    u64 *e = walk(root, va, true, 0);
    u64 v = (pa & PT_ADDR) | PT_PRESENT;
    if (flags & VMM_WRITE) v |= PT_WRITE;
    if (flags & VMM_USER)  v |= PT_USER;
    *e = v;
}

u64 vmm_translate(u64 root, u64 va) {
    u64 *e = walk(root, va, false, 0);
    if (!e || !(*e & PT_PRESENT)) return 0;
    return (*e & PT_ADDR) + (va & 0xFFF);
}

u64 vmm_new_user_root(void) {
    u64 root = pmm_alloc_page();
    u64 *kt = table(kernel_root), *ut = table(root);
    for (int i = 256; i < 512; i++) ut[i] = kt[i];   /* share kernel half */
    return root;
}

void vmm_activate(u64 root) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(root) : "memory");
}

void vmm_destroy_user_root(u64 root) {
    /* v0: free only the root page (leaf tables would leak — reaping
     * user spaces properly is on the roadmap). */
    pmm_free_page(root);
}

u64 arch_get_page_root(void) {
    u64 cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PT_ADDR;
}
void arch_set_page_root(u64 root) { vmm_activate(root); }
