CC      = gcc
CFLAGS  = -std=gnu11 -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -mno-red-zone -mgeneral-regs-only -mno-sse -mno-mmx \
          -fno-asynchronous-unwind-tables -Wall -Wno-unused-function -Wno-unused-variable
ISO     = nova-os.iso
APPS    = $(wildcard apps/*.exe)
HDR     = kernel/core.h kernel/input.h kernel/fs.h kernel/ui.h kernel/font.h kernel/ata.h kernel/store.h kernel/pe.h

all: $(ISO)

build/boot.o: boot/boot.S
	@mkdir -p build
	$(CC) -c $< -o $@

build/kernel.o: kernel/main.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -c kernel/main.c -o $@

build/kernel64.elf: build/boot.o build/kernel.o linker.ld
	ld -nostdlib -z max-page-size=0x1000 -T linker.ld -o $@ build/boot.o build/kernel.o

build/kernel.elf: build/kernel64.elf
	objcopy -O elf32-i386 $< $@

# image disk bootable (MBR+GRUB BIOS, ESP FAT16 UEFI, partisi data Nova) -> dipakai installer
build/novadisk.img: build/kernel.elf tools/mkdisk.py
	python3 tools/mkdisk.py --kernel build/kernel.elf --out $@

$(ISO): build/kernel.elf build/novadisk.img iso/boot/grub/grub.cfg $(APPS)
	@mkdir -p isodir/boot/grub isodir/apps
	cp build/kernel.elf isodir/boot/kernel.elf
	cp build/novadisk.img isodir/boot/novadisk.img
	@rm -f isodir/apps/*.exe; [ -z "$(APPS)" ] || cp $(APPS) isodir/apps/
	python3 tools/gengrubcfg.py iso/boot/grub/grub.cfg isodir/boot/grub/grub.cfg $(APPS)
	grub-mkrescue -o $@ isodir 2>&1 | tail -3

disk.img:
	truncate -s 512M $@

run: $(ISO) disk.img
	qemu-system-x86_64 -m 512 -cdrom $(ISO) -hda disk.img -boot d -vga std
run-disk: disk.img
	qemu-system-x86_64 -m 512 -hda disk.img -boot c -vga std
run-disk-uefi: disk.img
	qemu-system-x86_64 -m 512 -bios /usr/share/ovmf/OVMF.fd -hda disk.img -vga std

clean:
	rm -rf build isodir $(ISO)
