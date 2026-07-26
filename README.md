# ISUNUX — a from-scratch x86_64 kernel (UNIX-like kernel LARP arc)

This is milestone (yeah a milestone for me): a bare-metal x86_64 kernel that boots via the Limine
bootloader, runs in 64-bit long mode, and prints a hello-world message over
the COM1 serial port to prove it's alive.

## What's actually happening here

- `kernel/kernel.c` — the entire kernel right now. Sets up the Limine boot
  protocol requests, initializes the 16550 UART, and prints text.
- `kernel/linker.ld` — places the kernel in the higher half
  (`0xffffffff80000000`), the traditional Unix-y kernel/userspace split.
- `limine.conf` — tells the Limine bootloader where to find and how to load
  the kernel.
- `limine/` — a pinned copy of the Limine bootloader binaries/deploy tool
  (binary release branch, so no need to build the bootloader itself).
- `Makefile` — compiles the kernel freestanding (no libc, no red zone, no
  SSE, kernel code model), links it, builds a hybrid BIOS+UEFI bootable ISO,
  and can launch it in QEMU.

## Building & running

You need: `gcc`, `nasm`, `xorriso`, `qemu-system-x86_64`, `make` (all
installed already in this environment).

```sh
make          # just compile kernel.elf
make iso      # build myos.iso (bootable, BIOS + UEFI)
make run      # build + boot in QEMU headless, serial piped to your terminal
make clean    # nuke build artifacts
```

`make run` boots QEMU with `-display none -serial stdio`, so you'll see the
kernel's serial output directly in your terminal, no VNC/graphical window
needed. Ctrl+C to kill QEMU (the kernel halts forever in an `hlt` loop, so
it won't exit on its own).

## Where this goes next (the actual roadmap)

This is step 2 of the plan we talked through. In rough order:

1. ~~Boot + print hello world~~ ← you are here
2. GDT/IDT + interrupt handling (timer IRQ, keyboard IRQ)
3. Physical memory management (page frame allocator, using the Limine
   memmap request)
4. Virtual memory / paging management on top of what Limine hands you
5. Processes + a context switch + a dumb round-robin scheduler
6. Syscalls (`read`, `write`, `open`, `fork`, `exec`, `exit` first)
7. A minimal filesystem + VFS layer
8. Port a small libc (musl or your own) against your syscalls
9. Shell + coreutils running as real userspace processes

Each of those is its own multi-day-to-multi-week arc. Don't rush to step 9,
the fun part is actually steps 2-4.

## Milestone 2 progress: GDT + IDT + exception handling

- `kernel/gdt.c` / `gdt.h` — our own flat GDT (null, kernel code @ 0x08,
  kernel data @ 0x10), replacing the one Limine hands us. Loaded with
  `lgdt`, CS reloaded via the classic far-return trick since you can't
  `mov` into CS directly.
- `kernel/isr.asm` — one stub per CPU exception vector (0-31). Vectors
  that push a hardware error code get one macro (`ISR_ERR`), the rest get
  a dummy zero pushed so every frame has the same shape (`ISR_NOERR`).
  All 32 fall through into `isr_common`, which saves every general
  register, calls into C, restores, and `iretq`s back out.
- `kernel/idt.c` / `idt.h` — builds a 256-entry IDT, wires vectors 0-31 to
  the asm stubs, loads it with `lidt`. `interrupt_frame_t` in `idt.h` is
  the register layout `exception_handler` receives — it has to match the
  push order in `isr.asm` exactly, or you get garbage register dumps.
- `kernel/exceptions.c` — the actual handler. Decodes the vector number
  into a human name, prints vector/error-code/rip/cs/rflags over serial,
  then halts. This is what turns a silent QEMU reset into an actual
  readable crash report.
- `kernel/serial.c` / `serial.h` — serial driver pulled out of kernel.c
  into its own file, plus `serial_print_hex` / `serial_print_dec` for
  dumping register values.

`kernel.c` now calls `gdt_init()`, `idt_init()`, then deliberately
triggers a runtime (not compile-time-foldable) divide-by-zero to prove
the handler actually fires instead of the CPU triple-faulting and QEMU
silently rebooting.

## Milestone 2.5

- `kernel/pic.c` / `pic.h` — remaps the legacy 8259 PIC so IRQ0-15 land on
  interrupt vectors 32-47 instead of their default 8-15 (which collide
  with CPU exception vectors -- this MUST happen before `sti` or a timer
  tick would look like a CPU exception). Also masks everything except
  IRQ0 (timer), IRQ1 (keyboard), and IRQ2 (the master/slave cascade line,
  has to stay unmasked or the slave PIC's interrupts can never get
  through even though nothing's wired to it yet).
- `kernel/pit.c` / `pit.h` — programs the 8253/8254 PIT channel 0 to fire
  IRQ0 at a chosen frequency (100 Hz here, so a tick every 10ms).
- `kernel/isr.asm` — extended with 16 more stubs (vectors 32-47) that
  dispatch into `irq_common` / `irq_handler`, separate from the CPU
  exception path since hardware IRQs never carry a CPU-pushed error code.
- `kernel/irq.c` — the C-side dispatcher. Counts timer ticks (prints once
  a second), reads keyboard scancodes off port 0x60 and decodes
  pressed/released, sends EOI back to the PIC so it'll deliver the next
  interrupt.
- `kernel.c` now: gdt_init -> pic_remap -> idt_init -> pit_init -> `sti`.
  Order matters -- PIC and IDT must both be fully set up before
  interrupts are turned on.

Verified for real, not just by code review: booted in QEMU with serial
piped to a log file, then used QEMU's monitor to inject synthetic
keypresses (`sendkey a`, `sendkey shift-b`) while the kernel was running.
The keyboard IRQ handler correctly decoded scancodes 0x1e/0x9e (a
down/up) and 0x2a/0x30/0xb0/0xaa (shift down, b down, b up, shift up),
interleaved with the timer still ticking once a second in the
background -- proof the interrupt path handles concurrent IRQs from two
different devices correctly, not just one in isolation.

## ATTENTION

I did this after I read OSTEP and merely out of curiosity and boredom, so don't expect much lol

And I won't lie I use a lil help from AI :3