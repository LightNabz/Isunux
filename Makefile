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

C_SRCS := kernel/kernel.c kernel/serial.c kernel/gdt.c kernel/idt.c kernel/exceptions.c kernel/pic.c kernel/pit.c kernel/irq.c kernel/pmm.c kernel/vmm.c kernel/task.c kernel/tss.c kernel/syscall.c kernel/vfs.c kernel/tmpfs.c kernel/process.c kernel/elf.c kernel/userstack.c
ASM_SRCS := kernel/isr.asm kernel/switch.asm kernel/usermode.asm
OBJS := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

USER_PROG_ELF := kernel/userprog/hello_elf
USER_C_SRCS := kernel/userprog/hello.c kernel/userprog/mini_malloc.c kernel/userprog/mini_string.c kernel/userprog/mini_printf.c
USER_ASM_SRCS := kernel/userprog/crt0.asm
USER_OBJS := $(USER_C_SRCS:.c=.o) $(USER_ASM_SRCS:.asm=.o)
USER_PROG_BLOB_OBJ := kernel/userprog/hello_blob.o
OBJS += $(USER_PROG_BLOB_OBJ)

# Userspace test programs get their own, much simpler flag set -- no
# -mcmodel=kernel (that's a higher-half-kernel-only concern), no
# -mno-red-zone (nothing here handles interrupts on its own stack).
USER_CFLAGS := -g -O2 -Wall -Wextra \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-m64 \
	-fno-asynchronous-unwind-tables

USER_LDFLAGS := -nostdlib \
	-static \
	-m elf_x86_64 \
	-T kernel/userprog/user_link.ld

NASM := nasm
NASMFLAGS := -f elf64

.PHONY: all clean run iso

all: $(KERNEL)

%.o: %.c kernel/limine.h kernel/*.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

$(USER_PROG_ELF): $(USER_OBJS) kernel/userprog/user_link.ld
	$(LD) $(USER_LDFLAGS) $(USER_OBJS) -o $(USER_PROG_ELF)

kernel/userprog/%.o: kernel/userprog/%.c kernel/userprog/mini_libc.h kernel/userprog/mini_malloc.h kernel/userprog/mini_string.h kernel/userprog/mini_printf.h
	$(CC) $(USER_CFLAGS) -c $< -o $@

kernel/userprog/%.o: kernel/userprog/%.asm
	$(NASM) $(NASMFLAGS) $< -o $@

$(USER_PROG_BLOB_OBJ): kernel/userprog/hello_blob.asm $(USER_PROG_ELF)
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
	rm -rf $(OBJS) $(KERNEL) $(ISO) iso_root $(USER_PROG_ELF) $(USER_OBJS)
