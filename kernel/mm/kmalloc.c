/* Memoria — kernel heap. Simple free-list of 4 KiB slab classes
 * (16/32/64/128/256/512/1024/2048 B) carved out of PMM pages;
 * larger requests take whole pages. */
#include <kernel.h>

#define NUM_CLASSES 8
static const u16 class_size[NUM_CLASSES] = {16, 32, 64, 128, 256, 512, 1024, 2048};

struct freelist { struct freelist *next; };
struct slabhdr { u64 magic; u32 class_idx; u32 pad; struct freelist *free; };
struct bigalloc { u64 magic; u64 pages; };
#define SLAB_MAGIC 0xAE0511ABAE0511ABULL
#define BIG_MAGIC  0xAE051010AE051010ULL

static struct freelist *class_head[NUM_CLASSES];

void kmalloc_init(void) {
    memset(class_head, 0, sizeof(class_head));
    early_printk("kmalloc: ready\n");
}

static int size_to_class(size_t n) {
    for (int i = 0; i < NUM_CLASSES; i++)
        if (n <= class_size[i]) return i;
    return -1;
}

static void refill(int cls) {
    u64 pa = pmm_alloc_page();
    struct slabhdr *s = phys_to_virt(pa);
    s->magic = SLAB_MAGIC;
    s->class_idx = cls;
    int sz = class_size[cls];
    int first = ALIGN_UP(sizeof(*s), sz);
    s->free = NULL;
    for (int off = first; off + sz <= (int)PAGE_SIZE; off += sz) {
        struct freelist *f = (struct freelist *)((u8 *)s + off);
        f->next = s->free;
        s->free = f;
    }
    class_head[cls] = s->free;
}

void *kmalloc(size_t n) {
    if (n == 0) return NULL;
    int cls = size_to_class(n);
    if (cls < 0) {
        u64 pages = ALIGN_UP(n, PAGE_SIZE) / PAGE_SIZE + 1;
        u64 pa = pmm_alloc_contig(pages);
        if (!pa) panic("kmalloc: OOM for %llu bytes", (unsigned long long)n);
        struct bigalloc *b = phys_to_virt(pa);
        b->magic = BIG_MAGIC;
        b->pages = pages;
        return (u8 *)b + PAGE_SIZE;
    }
    if (!class_head[cls]) refill(cls);
    struct freelist *f = class_head[cls];
    class_head[cls] = f->next;
    memset(f, 0, class_size[cls]);
    return f;
}

void *kzalloc(size_t n) { return kmalloc(n); }

void kfree(void *p) {
    if (!p) return;
    u64 pa = virt_to_phys(p);
    u64 page_base_pa = pa & ~(u64)(PAGE_SIZE - 1);

    if (pa % PAGE_SIZE == 0) {
        /* big allocations sit one page behind their header page */
        struct bigalloc *b = phys_to_virt(page_base_pa - PAGE_SIZE);
        if (b->magic == BIG_MAGIC) {
            for (u64 i = 0; i < b->pages; i++)
                pmm_free_page((page_base_pa - PAGE_SIZE) + i * PAGE_SIZE);
            return;
        }
    }

    struct slabhdr *s = phys_to_virt(page_base_pa);
    if (s->magic == SLAB_MAGIC && s->class_idx < NUM_CLASSES) {
        struct freelist *f = p;
        int cls = s->class_idx;
        f->next = class_head[cls];
        class_head[cls] = f;
    }
    /* else: unknown pointer — leak (v0 heap is forgiving) */
}
