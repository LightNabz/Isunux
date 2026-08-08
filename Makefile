override KERNEL := kernel.elf
override ISO := isunux.iso
override DISK := disk.img

CC := gcc
LD := ld
NASM := nasm
NASMFLAGS := -f elf64

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

# Userspace programs get their own, much simpler flag set -- no
# -mcmodel=kernel (that's a higher-half-kernel-only concern), no
# -mno-red-zone (nothing here handles interrupts on its own stack), and
# SSE stays disabled for the same reason the kernel disables it: we
# never enable CR4.OSFXSR, so any SSE instruction is a real #UD.
# -Ilib is what lets every bin/<name>/main.c say #include "mini_libc.h"
# unchanged, without needing a relative "../../lib/" in every program.
USER_CFLAGS := -g -O2 -Wall -Wextra \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-m64 \
	-mno-80387 \
	-mno-mmx \
	-mno-3dnow \
	-mno-sse \
	-mno-sse2 \
	-fno-asynchronous-unwind-tables \
	-Ilib

USER_LDFLAGS := -nostdlib \
	-static \
	-m elf_x86_64 \
	-T lib/user_link.ld

C_SRCS := kernel/kernel.c kernel/serial.c kernel/term.c kernel/fb.c kernel/gdt.c kernel/idt.c kernel/exceptions.c kernel/pic.c kernel/pit.c kernel/irq.c kernel/pmm.c kernel/vmm.c kernel/task.c kernel/tss.c kernel/syscall.c kernel/vfs.c kernel/tmpfs.c kernel/devfs.c kernel/pipe.c kernel/process.c kernel/elf.c kernel/userstack.c kernel/fork.c kernel/exec.c kernel/keyboard.c kernel/ata.c
ASM_SRCS := kernel/isr.asm kernel/switch.asm kernel/usermode.asm
OBJS := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

# ---- userspace: every program under bin/ ----
#
# Every directory under bin/ IS a program -- no list to maintain here,
# nothing to edit to add one. bin/<name>/main.c gets built into
# bin/<name>/<name>, a real standalone ELF, using the shared runtime in
# lib/ (malloc/string/printf + crt0, compiled once and reused by every
# program, same as any real libc would be). That ELF is what actually
# ships: staged onto the ISO at iso_root/bin/<name> and declared as a
# Limine boot module in the generated limine.conf (see the `iso` rule)
# -- never incbin'd into the kernel image itself. Adding a program is
# "add a directory", full stop; kernel.elf never gets relinked for it.
USER_PROGRAMS := $(patsubst bin/%/,%,$(wildcard bin/*/))

USER_RUNTIME_SRCS := lib/mini_malloc.c lib/mini_string.c lib/mini_printf.c
USER_RUNTIME_OBJS := $(USER_RUNTIME_SRCS:.c=.o)
USER_CRT0_OBJ := lib/crt0.o

USER_PROG_OBJS := $(foreach p,$(USER_PROGRAMS),bin/$(p)/main.o)
USER_ELFS      := $(foreach p,$(USER_PROGRAMS),bin/$(p)/$(p))

.PHONY: all clean run iso

all: $(KERNEL) $(USER_ELFS)

%.o: %.c kernel/limine.h kernel/*.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

lib/%.o: lib/%.c lib/mini_libc.h lib/mini_malloc.h lib/mini_string.h lib/mini_printf.h
	$(CC) $(USER_CFLAGS) -c $< -o $@

# each program's own rules -- generated per-name because the final
# binary's path repeats its name twice (bin/ls/ls), which a plain
# single-%-per-pattern rule can't express
define USERPROG_template
bin/$(1)/main.o: bin/$(1)/main.c lib/mini_libc.h lib/mini_malloc.h lib/mini_string.h lib/mini_printf.h
	$$(CC) $$(USER_CFLAGS) -c $$< -o $$@

bin/$(1)/$(1): bin/$(1)/main.o $$(USER_RUNTIME_OBJS) $$(USER_CRT0_OBJ) lib/user_link.ld
	$$(LD) $$(USER_LDFLAGS) $$< $$(USER_RUNTIME_OBJS) $$(USER_CRT0_OBJ) -o $$@
endef

$(foreach p,$(USER_PROGRAMS),$(eval $(call USERPROG_template,$(p))))

$(KERNEL): $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL)

iso: $(KERNEL) $(USER_ELFS)
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT iso_root/bin
	cp -v $(KERNEL) iso_root/boot/kernel.elf
	for p in $(USER_PROGRAMS); do \
		cp -v bin/$$p/$$p iso_root/bin/$$p; \
	done
	cp boot/limine.conf.in iso_root/boot/limine/limine.conf
	for p in $(USER_PROGRAMS); do \
		echo "    module_path: boot():/bin/$$p" >> iso_root/boot/limine/limine.conf; \
	done
	cp -v boot/limine/limine-bios.sys boot/limine/limine-bios-cd.bin boot/limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v boot/limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v boot/limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(ISO)
	./boot/limine/limine bios-install $(ISO)

run: iso $(DISK)
	qemu-system-x86_64 -M q35 -m 256M -cdrom $(ISO) -boot d \
		-device piix3-ide,id=ide -drive id=disk,file=$(DISK),if=none,format=raw \
		-device ide-hd,drive=disk,bus=ide.0,unit=0 \
		-serial stdio -display none -no-reboot -no-shutdown

gui: iso $(DISK)
	qemu-system-x86_64 -M q35 -m 256M -cdrom $(ISO) -boot d \
		-device piix3-ide,id=ide -drive id=disk,file=$(DISK),if=none,format=raw \
		-device ide-hd,drive=disk,bus=ide.0,unit=0 \
		-serial stdio -display sdl -no-reboot -no-shutdown

# The persistent disk ATA drives against. Deliberately has NO
# prerequisites -- this rule only ever fires when disk.img doesn't
# exist at all, never because something "newer" touched it, so it's
# safe to run 'make run' over and over across reboots without ever
# silently re-zeroing whatever's actually been written to the disk.
# 'clean' below deliberately does NOT remove this file, for the same
# reason -- see its comment.
#
# q35 has no legacy IDE controller by default (its native SATA
# controller is AHCI) -- piix3-ide is added explicitly here so the
# primary channel answers on the classic 0x1F0/0x3F6 ports this
# driver expects, without needing PCI enumeration to find anything
# (see kernel/ata.h's top comment for why that's the point).
$(DISK):
	dd if=/dev/zero of=$(DISK) bs=1M count=16

clean:
	rm -rf $(OBJS) $(KERNEL) $(ISO) iso_root $(USER_PROG_OBJS) $(USER_ELFS) $(USER_RUNTIME_OBJS) $(USER_CRT0_OBJ)
	# deliberately NOT removing $(DISK) here -- it's persistent user
	# data, not a build artifact. Use 'rm disk.img' by hand if you
	# actually want a fresh, blank disk.
