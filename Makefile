# AequiOS — one build, two architectures.
#   make ARCH=x86_64     → build/x86_64/aequios-x86_64.iso   (BIOS + UEFI)
#   make ARCH=aarch64    → build/aarch64/aequios-aarch64.img (UEFI ESP image)
#   make run ARCH=...    → boot it in QEMU
#   make test            → scripted verification on both arches

ARCH ?= x86_64
BUILD := build/$(ARCH)
CC    := clang
LD    := ld.lld

ifeq ($(ARCH),x86_64)
TARGET    := x86_64-unknown-none
ARCHDIR   := kernel/arch/x86_64
CFLAGS_ARCH := -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2
KERNEL_LD := kernel/linker-x86_64.ld
USER_LD   := userspace/user-x86_64.ld
ENTRY_S   := userspace/lib/entry_x86_64.S
endif

ifeq ($(ARCH),aarch64)
TARGET    := aarch64-unknown-none
ARCHDIR   := kernel/arch/aarch64
CFLAGS_ARCH := -mgeneral-regs-only
KERNEL_LD := kernel/linker-aarch64.ld
USER_LD   := userspace/user-aarch64.ld
ENTRY_S   := userspace/lib/entry_aarch64.S
endif

CFLAGS  := --target=$(TARGET) -ffreestanding -fno-builtin -fno-stack-protector \
           -fno-stack-check -fno-pic -fno-pie -O2 -g -Wall -Wextra \
           -Ikernel/include -Iuserspace/include $(CFLAGS_ARCH)
LDFLAGS := -nostdlib -static --no-dynamic-linker -z max-page-size=0x1000

KSRC := $(wildcard kernel/core/*.c kernel/mm/*.c kernel/fs/*.c kernel/proc/*.c \
        kernel/sys/*.c drivers/*.c $(ARCHDIR)/*.c)
KASM := $(wildcard $(ARCHDIR)/*.S)
KOBJ := $(patsubst %.c,$(BUILD)/%.o,$(KSRC)) \
        $(patsubst %.S,$(BUILD)/%.o,$(KASM)) \
        $(BUILD)/drivers/font_data.o

LIMINE := third_party/limine

.PHONY: all clean run test image limine
all: image

limine:
	@test -d $(LIMINE) || tools/fetch_limine.sh $(LIMINE)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/drivers/font_data.c: tools/mkfont.py
	@mkdir -p $(dir $@)
	python3 tools/mkfont.py $@

$(BUILD)/drivers/font_data.o: $(BUILD)/drivers/font_data.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/aequios.elf: $(KERNEL_LD) $(KOBJ)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) $(KOBJ) -o $@

# ---- userspace ----
$(BUILD)/usr-%.o: userspace/lib/%.S
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usr-ratio-main.o: userspace/ratio/main.c
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/usr-linuxhello-main.o: userspace/linuxhello/main.c
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/usr-linuxhello-entry.o: userspace/linuxhello/entry.S
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usr-entry.o: $(ENTRY_S)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usr/ratio: $(BUILD)/usr-entry.o $(BUILD)/usr-ratio-main.o \
                    $(BUILD)/usr-ulib.o $(USER_LD)
	@mkdir -p $(BUILD)/usr
	$(LD) $(LDFLAGS) -T $(USER_LD) -o $@ $(BUILD)/usr-entry.o \
	      $(BUILD)/usr-ratio-main.o $(BUILD)/usr-ulib.o

$(BUILD)/usr/linux-hello: $(BUILD)/usr-linuxhello-entry.o \
                          $(BUILD)/usr-linuxhello-main.o $(USER_LD)
	@mkdir -p $(BUILD)/usr
	$(LD) $(LDFLAGS) -T $(USER_LD) -o $@ $(BUILD)/usr-linuxhello-entry.o \
	      $(BUILD)/usr-linuxhello-main.o

$(BUILD)/usr-ulib.o: userspace/lib/ulib.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---- initramfs ----
$(BUILD)/initramfs.tar: $(BUILD)/usr/ratio $(BUILD)/usr/linux-hello tools/mkinitramfs.py
	@rm -rf $(BUILD)/initramfs-root
	@mkdir -p $(BUILD)/initramfs-root/bin $(BUILD)/initramfs-root/etc
	cp $(BUILD)/usr/ratio       $(BUILD)/initramfs-root/bin/ratio
	cp $(BUILD)/usr/linux-hello $(BUILD)/initramfs-root/bin/linux-hello
	printf 'NAME="AequiOS"\nID=aequios\nVERSION_ID="0.1"\nPRETTY_NAME="AequiOS 0.1 - Ordo kernel"\n' \
	  > $(BUILD)/initramfs-root/etc/os-release
	printf 'Welcome to AequiOS. Even in architecture, design, and spirit.\n' \
	  > $(BUILD)/initramfs-root/etc/motd
	python3 tools/mkinitramfs.py $(BUILD)/initramfs-root $@

# ---- boot media ----
image: $(BUILD)/aequios.elf $(BUILD)/initramfs.tar limine
	@rm -rf $(BUILD)/iso-root
	@mkdir -p $(BUILD)/iso-root/boot
	cp $(BUILD)/aequios.elf $(BUILD)/initramfs.tar boot/limine.conf \
	   $(BUILD)/iso-root/boot/
ifeq ($(ARCH),x86_64)
	cp $(LIMINE)/limine-bios.sys $(LIMINE)/limine-bios-cd.bin \
	   $(LIMINE)/limine-uefi-cd.bin $(BUILD)/iso-root/
	mkdir -p $(BUILD)/iso-root/EFI/BOOT
	cp $(LIMINE)/BOOTX64.EFI $(BUILD)/iso-root/EFI/BOOT/
	xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
	  -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part \
	  --efi-boot-image --protective-msdos-label \
	  $(BUILD)/iso-root -o $(BUILD)/aequios-$(ARCH).iso -quiet -V AEQUIOS
	$(LIMINE)/limine bios-install $(BUILD)/aequios-$(ARCH).iso
	@echo "→ $(BUILD)/aequios-$(ARCH).iso"
endif
ifeq ($(ARCH),aarch64)
	mkdir -p $(BUILD)/iso-root/EFI/BOOT
	cp $(LIMINE)/BOOTAA64.EFI $(BUILD)/iso-root/EFI/BOOT/
	dd if=/dev/zero of=$(BUILD)/esp.img bs=1M count=64 2>/dev/null
	mformat -i $(BUILD)/esp.img -F ::
	mmd -i $(BUILD)/esp.img ::/EFI ::/EFI/BOOT ::/boot 2>/dev/null || true
	mcopy -i $(BUILD)/esp.img $(BUILD)/iso-root/EFI/BOOT/BOOTAA64.EFI ::/EFI/BOOT/
	mcopy -i $(BUILD)/esp.img $(BUILD)/iso-root/boot/aequios.elf \
	      $(BUILD)/iso-root/boot/initramfs.tar $(BUILD)/iso-root/boot/limine.conf ::/boot/
	mv $(BUILD)/esp.img $(BUILD)/aequios-$(ARCH).img
	@echo "→ $(BUILD)/aequios-$(ARCH).img"
endif

run: image
ifeq ($(ARCH),x86_64)
	qemu-system-x86_64 -M q35 -m 512M -serial stdio -display none \
	  -cdrom $(BUILD)/aequios-$(ARCH).iso
endif
ifeq ($(ARCH),aarch64)
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512M \
	  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
	  -drive format=raw,file=$(BUILD)/aequios-$(ARCH).img \
	  -device ramfb -serial stdio -display none
endif

test:
	python3 tools/vmtest.py x86_64
	python3 tools/vmtest.py aarch64

clean:
	rm -rf build
