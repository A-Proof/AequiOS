/* aarch64 — paging on qemu virt (4 KiB granule, 4 levels).
 * Kernel space lives in TTBR1_EL1 (Limine's tables); each user task gets
 * a private TTBR0_EL1. During SVC handling the kernel half stays mapped
 * through TTBR1 regardless of the active TTBR0 — no root switch needed
 * on entry. */
#include <kernel.h>

/* descriptor bits */
#define D_TABLE   0x3ULL
#define D_PAGE    0x3ULL
#define D_AF      (1ULL << 10)
#define D_SH_INNER (3ULL << 8)
#define D_AP_RO_EL1  (2ULL << 6)
#define D_AP_RW_ALL  (1ULL << 6)
#define D_UXN     (1ULL << 54)
#define D_PXN     (1ULL << 53)
#define D_ADDR    0x0000FFFFFFFFF000ULL

static u64 kernel_root;         /* TTBR1_EL1 base */
static u64 mair_normal_idx = 0, mair_device_idx = 1;

static inline u64 *table(u64 phys) { return phys_to_virt(phys); }

static u64 mair_read(void) {
    u64 v;
    __asm__ volatile ("mrs %0, mair_el1" : "=r"(v));
    return v;
}

static int mair_find(u8 want_min, u8 want_max) {
    u64 mair = mair_read();
    early_printk("mair_el1 = %p\n", (void *)mair);
    for (int i = 0; i < 8; i++) {
        u8 a = (mair >> (i * 8)) & 0xFF;
        if (a >= want_min && a <= want_max) return i;
    }
    return -1;
}

void vmm_init(void) {
    u64 ttbr1, tcr;
    __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(ttbr1));
    __asm__ volatile ("mrs %0, tcr_el1" : "=r"(tcr));
    kernel_root = ttbr1 & D_ADDR;
    early_printk("vmm: ttbr1=%p tcr=%p\n", (void *)ttbr1, (void *)tcr);

    /* device-memory attr index (nGnRnE=0x00, nGnRE=0x04, nGRE=0x08, GRE=0x0C) */
    int d = mair_find(0x00, 0x0C);
    int n = mair_find(0x40, 0xFF);           /* normal cacheable attr ~0xFF/0x44 */
    if (d >= 0) mair_device_idx = d;
    if (n >= 0) mair_normal_idx = n;
    early_printk("vmm: mair normal=%llu device=%llu\n",
                 (unsigned long long)mair_normal_idx,
                 (unsigned long long)mair_device_idx);
}

static u64 *walk(u64 root, u64 va, bool create) {
    u64 *t = table(root);
    for (int l = 0; l < 3; l++) {
        int idx = (va >> (39 - l * 9)) & 0x1FF;
        u64 e = t[idx];
        if ((e & 0x3) != 0x3) {
            if (!create) return NULL;
            u64 nt = pmm_alloc_page();
            t[idx] = (nt & D_ADDR) | D_TABLE;
            e = t[idx];
        }
        t = table(e & D_ADDR);
    }
    return &t[(va >> 12) & 0x1FF];
}

void vmm_map(u64 root, u64 va, u64 pa, u64 flags) {
    u64 *e = walk(root, va, true);
    u64 v = (pa & D_ADDR) | D_PAGE | D_AF;
    if (flags & VMM_DEVICE) {
        v |= (mair_device_idx << 2);         /* attr index */
        v |= D_UXN | D_PXN;
    } else if (flags & VMM_USER) {
        v |= (mair_normal_idx << 2) | D_SH_INNER | D_AP_RW_ALL | D_PXN;
        if (!(flags & VMM_EXEC)) v |= D_UXN;
    } else {
        v |= (mair_normal_idx << 2) | D_SH_INNER | D_UXN | D_PXN;
        if (!(flags & VMM_WRITE)) v |= D_AP_RO_EL1;
    }
    *e = v;
}

u64 vmm_translate(u64 root, u64 va) {
    u64 *t = table(root);
    for (int l = 0; l < 4; l++) {
        u64 e = t[(va >> (39 - l * 9)) & 0x1FF];
        if ((e & 0x3) == 0x3 && l < 3) { t = table(e & D_ADDR); continue; }
        if ((e & 0x3) == 0x1) {           /* block */
            u64 mask = (1ULL << (39 - l * 9)) - 1;
            return (e & D_ADDR & ~mask) + (va & mask);
        }
        if ((e & 0x3) == 0x3 && l == 3)
            return (e & D_ADDR) + (va & 0xFFF);
        return 0;
    }
    return 0;
}

u64 vmm_new_user_root(void) {
    /* TTBR0: fresh empty table — kernel half stays on TTBR1 */
    return pmm_alloc_page();
}

void vmm_activate(u64 root) {
    __asm__ volatile (
        "msr ttbr0_el1, %0\n"
        "tlbi vmalle1\n"
        "dsb sy\n"
        "isb\n"
        :: "r"(root) : "memory");
}

void vmm_destroy_user_root(u64 root) { pmm_free_page(root); }

u64 arch_get_page_root(void) {
    u64 v;
    __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(v));
    return v & D_ADDR;
}
void arch_set_page_root(u64 root) { vmm_activate(root); }

u64 vmm_kernel_root(void) { return kernel_root; }
u64 vmm_device_attr_idx(void) { return mair_device_idx; }
