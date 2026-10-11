/* x86_64 — GDT + TSS (ring0 stack for user exceptions). */
#include <kernel.h>

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct gdt_ext_entry {  /* TSS descriptor is 16 bytes */
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct tss {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iopb;
} __attribute__((packed));

static struct {
    struct gdt_entry     entries[5];     /* null, kcode, kdata, udata32, udata */
    struct gdt_entry     ucode64;        /* index 5 */
    struct gdt_ext_entry tss;            /* index 6-7 */
} __attribute__((packed)) gdt;

static struct tss tss;

struct gdtr { u16 limit; u64 base; } __attribute__((packed));

static void set_entry(struct gdt_entry *e, u8 access, u8 gran) {
    e->limit_low = 0xFFFF;
    e->base_low = e->base_mid = e->base_high = 0;
    e->access = access;
    e->granularity = gran;
}

#define ACC_PRESENT 0x80
#define ACC_RING3   0x60
#define ACC_CODE    0x18   /* exec + accessed/type bits */
#define ACC_DATA    0x12
#define GRAN_LONG   0x20   /* L bit */
#define GRAN_4K     0x80

extern u8 kernel_stack_top[];

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    set_entry(&gdt.entries[1], ACC_PRESENT | ACC_CODE, GRAN_4K | GRAN_LONG | 0x0F); /* 0x08 kcode */
    set_entry(&gdt.entries[2], ACC_PRESENT | ACC_DATA, GRAN_4K | 0x0F);             /* 0x10 kdata */
    set_entry(&gdt.entries[3], ACC_PRESENT | ACC_RING3 | ACC_DATA, GRAN_4K | 0x0F); /* 0x18 rfu */
    set_entry(&gdt.entries[4], ACC_PRESENT | ACC_RING3 | ACC_DATA, GRAN_4K | 0x0F); /* 0x20 udata */
    set_entry(&gdt.ucode64,    ACC_PRESENT | ACC_RING3 | ACC_CODE, GRAN_4K | GRAN_LONG | 0x0F); /* 0x28 ucode */

    u64 tss_base = (u64)&tss;
    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = (u64)kernel_stack_top;
    tss.iopb = sizeof(tss);
    gdt.tss.limit_low = sizeof(tss) - 1;
    gdt.tss.base_low = tss_base & 0xFFFF;
    gdt.tss.base_mid = (tss_base >> 16) & 0xFF;
    gdt.tss.access = 0x89;               /* present | 64-bit TSS */
    gdt.tss.granularity = 0;
    gdt.tss.base_high = (tss_base >> 24) & 0xFF;
    gdt.tss.base_upper = tss_base >> 32;
    gdt.tss.reserved = 0;

    struct gdtr r = { sizeof(gdt) - 1, (u64)&gdt };
    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov $0x30, %%ax\n"
        "ltr %%ax\n"
        :: "m"(r) : "rax", "memory");
}
