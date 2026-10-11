/* Memoria — physical memory manager.
 * Bitmap allocator over Limine's memmap, one bit per 4 KiB page. */
#include <kernel.h>

static u8 *bitmap;
static u64 bitmap_pages;         /* pages covered by the bitmap */
static u64 total_pages, free_pages;
static u64 highest_page;         /* highest frame index + 1 */

extern volatile struct limine_memmap_request memmap_request; /* unused */

static inline void bit_set(u64 i)   { bitmap[i >> 3] |=  (u8)(1 << (i & 7)); }
static inline void bit_clear(u64 i) { bitmap[i >> 3] &= ~(u8)(1 << (i & 7)); }
static inline bool bit_test(u64 i)  { return bitmap[i >> 3] & (u8)(1 << (i & 7)); }

void pmm_init(struct limine_memmap_response *mm) {
    u64 top = 0;
    for (u64 i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        u64 end = e->base + e->length;
        if (end > top) top = end;
    }
    highest_page = ALIGN_UP(top, PAGE_SIZE) / PAGE_SIZE;
    bitmap_pages = highest_page;
    u64 bitmap_bytes = ALIGN_UP(bitmap_pages, 8) / 8;

    /* find a home for the bitmap */
    for (u64 i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE || e->length < bitmap_bytes) continue;
        bitmap = phys_to_virt(e->base);
        break;
    }
    if (!bitmap) panic("pmm: nowhere to place bitmap");

    memset(bitmap, 0xFF, bitmap_bytes);   /* everything reserved... */

    total_pages = free_pages = 0;
    for (u64 i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        u64 first = ALIGN_UP(e->base, PAGE_SIZE) / PAGE_SIZE;
        u64 pages = ALIGN_DOWN(e->length, PAGE_SIZE) / PAGE_SIZE;
        for (u64 p = first; p < first + pages && p < bitmap_pages; p++) {
            bit_clear(p);
            free_pages++;
            total_pages++;
        }
    }

    /* re-reserve the bitmap itself */
    u64 bpa = virt_to_phys(bitmap) / PAGE_SIZE;
    u64 bpn = ALIGN_UP(bitmap_bytes, PAGE_SIZE) / PAGE_SIZE;
    for (u64 p = bpa; p < bpa + bpn; p++) { if (!bit_test(p)) { bit_set(p); free_pages--; } }

    /* reserve page 0 (null) */
    if (!bit_test(0)) { bit_set(0); free_pages--; }

    early_printk("pmm: %llu MiB usable (%llu pages)\n",
                 (unsigned long long)(total_pages * PAGE_SIZE / MB),
                 (unsigned long long)total_pages);
}

u64 pmm_alloc_page(void) {
    for (u64 i = 0; i < bitmap_pages; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            free_pages--;
            memset(phys_to_virt(i * PAGE_SIZE), 0, PAGE_SIZE);
            return i * PAGE_SIZE;
        }
    }
    panic("pmm: out of memory");
    return 0;
}

void pmm_free_page(u64 pa) {
    u64 i = pa / PAGE_SIZE;
    if (i < bitmap_pages && bit_test(i)) { bit_clear(i); free_pages++; }
}

u64 pmm_alloc_contig(u64 pages) {
    u64 run = 0, start = 0;
    for (u64 i = 0; i < bitmap_pages; i++) {
        if (!bit_test(i)) {
            if (run == 0) start = i;
            if (++run == pages) {
                for (u64 p = start; p < start + pages; p++) { bit_set(p); free_pages--; }
                memset(phys_to_virt(start * PAGE_SIZE), 0, pages * PAGE_SIZE);
                return start * PAGE_SIZE;
            }
        } else run = 0;
    }
    return 0;
}

u64 pmm_total_pages(void) { return total_pages; }
u64 pmm_free_pages(void)  { return free_pages; }
u64 pmm_total_bytes(void) { return total_pages * PAGE_SIZE; }
u64 pmm_free_bytes(void)  { return free_pages * PAGE_SIZE; }
