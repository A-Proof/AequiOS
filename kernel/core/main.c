/* Ordo — the AequiOS kernel.
 * Layers (see website): Initium (boot, via Limine) → Ordo (kernel core)
 * → Memoria (pmm/vmm/heap) → Vita (tasks) → LumenFS (initramfs v0)
 * → Serenus (userspace: Aura graphics, Ratio shell).
 */
#include <kernel.h>

u64 hhdm_offset;

extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_module_request module_request;
extern volatile struct limine_executable_address_request executable_address_request;
extern volatile struct limine_bootloader_info_request bootloader_info_request;

extern volatile uint64_t limine_base_revision[];

void kmain(void) {
    serial_init();
    early_printk("\n=== AequiOS kernel (Ordo) ===\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
        panic("limine base revision 6 unsupported");

    if (hhdm_request.response == NULL) panic("no hhdm");
    hhdm_offset = hhdm_request.response->offset;
    early_printk("hhdm offset = %p\n", (void *)hhdm_offset);

    if (memmap_request.response == NULL) panic("no memmap");
    if (executable_address_request.response == NULL) panic("no exec addr");

    struct limine_executable_address_response *ea = executable_address_request.response;
    early_printk("kernel phys=%p virt=%p\n",
                 (void *)ea->physical_base, (void *)ea->virtual_base);

    /* Memoria */
    vmm_init();
    pmm_init(memmap_request.response);
    kmalloc_init();

    /* arch-specific post-memory init (MMIO maps, vectors, syscalls) */
    arch_early_init();
    arch_late_init();

    /* framebuffer + splash */
    fb_init();
    fb_splash();
    input_init();
    printk_console_ready();

    printk("AequiOS: Even in Architecture, Design, and Spirit\n");

    /* LumenFS v0: initramfs (ustar module) */
    if (vfs_init() != 0) panic("initramfs missing");

    /* desktop chrome — wallpaper, menu bar, dock, terminal window;
     * the console renders inside the window body */
    desktop_init();

    /* Vita: drop to ring 3 / EL0 into the Ratio shell. */
    struct task *init = task_spawn("/bin/ratio");
    if (!init) panic("could not spawn /bin/ratio");

    printk("[ordo] entering userspace\n");
    task_run(init);   /* never returns */

    panic("task_run returned");
}
