override KERNEL := kernel.elf
override ISO := myos.iso

CC := gcc
LD := ld

CFLAGS := -g -O2 -Wall -Wextra \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-pie \
	-fno-pic \
	-m64 \
	-march=x86-64 \
	-mno-80387 \
	-mno-mmx \
	-mno-3dnow \
	-mno-sse \
	-mno-sse2 \
	-mno-red-zone \
	-mcmodel=kernel

LDFLAGS := -nostdlib \
	-static \
	-m elf_x86_64 \
	-z max-page-size=0x1000 \
	-T kernel/linker.ld

C_SRCS := kernel/kernel.c kernel/serial.c kernel/gdt.c kernel/idt.c kernel/exceptions.c
ASM_SRCS := kernel/isr.asm
OBJS := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

NASM := nasm
NASMFLAGS := -f elf64

.PHONY: all clean run iso

all: $(KERNEL)

%.o: %.c kernel/limine.h kernel/*.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL): $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL)

iso: $(KERNEL)
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp -v $(KERNEL) iso_root/boot/kernel.elf
	cp -v limine.conf iso_root/boot/limine/
	cp -v limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(ISO)
	./limine/limine bios-install $(ISO)

run: iso
	qemu-system-x86_64 -M q35 -m 256M -cdrom $(ISO) -boot d \
		-serial stdio -display none -no-reboot -no-shutdown

clean:
	rm -rf $(OBJS) $(KERNEL) $(ISO) iso_root
