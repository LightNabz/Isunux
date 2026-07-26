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

- `kernel/pmm.c` / `pmm.h` — a bitmap allocator over physical RAM. One
  bit per 4KiB page: 1 = used/unavailable, 0 = free. The bitmap is a
  static array covering up to 4 GiB (`MAX_PHYS_MEMORY`), sized that way
  deliberately so this milestone doesn't need a working heap or HHDM
  access to stand itself up -- simplest version that's still correct.
- `pmm_init()` starts by marking *everything* used, then walks Limine's
  memory map and only clears the bits for regions explicitly typed
  `LIMINE_MEMMAP_USABLE`. This is what makes exclusion of the kernel
  image, ACPI tables, MMIO holes, etc. automatic -- we never have to
  special-case "don't hand out the kernel's own pages," Limine already
  told us those aren't usable.
- `pmm_alloc_page()` / `pmm_free_page()` -- linear bitmap scan with a
  `scan_hint` so repeated allocations don't rescan memory that's
  permanently in use. `pmm_free_page()` resets the hint backward so the
  next allocation reuses the freed page instead of continuing to march
  forward.
- `kernel.c` now requests Limine's memmap, prints every region (base,
  length, type), initializes the PMM, prints total/free/used stats, then
  runs a real test: allocate 5 pages, free the third one, allocate
  again, and check the returned address matches the freed one.

Verified for real: booted in QEMU, got an actual 18-entry memory map from
Limine, ~254 MiB usable out of the 256M given to the VM, and watched the
allocator hand back the exact freed address (0x55000) on the next
`pmm_alloc_page()` call after freeing it.

## Milestone 3 complete: physical memory manager

Added since the last update:

- `kernel/pmm.c` / `pmm.h` — a bitmap allocator over physical RAM. One
  bit per 4KiB page: 1 = used/unavailable, 0 = free. The bitmap is a
  static array covering up to 4 GiB (`MAX_PHYS_MEMORY`), sized that way
  deliberately so this milestone doesn't need a working heap or HHDM
  access to stand itself up -- simplest version that's still correct.
- `pmm_init()` starts by marking *everything* used, then walks Limine's
  memory map and only clears the bits for regions explicitly typed
  `LIMINE_MEMMAP_USABLE`. This is what makes exclusion of the kernel
  image, ACPI tables, MMIO holes, etc. automatic -- we never have to
  special-case "don't hand out the kernel's own pages," Limine already
  told us those aren't usable.
- `pmm_alloc_page()` / `pmm_free_page()` -- linear bitmap scan with a
  `scan_hint` so repeated allocations don't rescan memory that's
  permanently in use. `pmm_free_page()` resets the hint backward so the
  next allocation reuses the freed page instead of continuing to march
  forward.
- `kernel.c` now requests Limine's memmap, prints every region (base,
  length, type), initializes the PMM, prints total/free/used stats, then
  runs a real test: allocate 5 pages, free the third one, allocate
  again, and check the returned address matches the freed one.

Verified for real: booted in QEMU, got an actual 18-entry memory map from
Limine, ~254 MiB usable out of the 256M given to the VM, and watched the
allocator hand back the exact freed address (0x55000) on the next
`pmm_alloc_page()` call after freeing it.

## Milestone 4 complete: virtual memory / paging

Added since the last update:

- `kernel/linker.ld` — added `kernel_virt_start` / `kernel_virt_end`
  symbols bracketing the kernel image, so the VMM knows the exact size
  to map instead of guessing a round number.
- `kernel/vmm.c` / `vmm.h` — our own 4-level page tables (PML4 -> PDPT ->
  PD -> PT). Two mapping paths:
  - `vmm_map_4k()` walks all 4 levels, allocating intermediate tables
    via the PMM as needed, for precise page-level mappings (used for the
    kernel image).
  - `map_2m()` (internal) stops at the PD level and sets the huge-page
    (PS) bit directly, skipping the PT level entirely. Used to map the
    entire HHDM window (physical memory 0..4GiB, matching what the PMM
    tracks) in one pass without burning thousands of pages on page
    tables just for that.
  - `vmm_init()` allocates a fresh PML4, maps the HHDM window + the
    kernel image (using the exact size from the linker symbols), then
    switches `CR3` to the new tables.
- `kernel/exceptions.c` — page faults (vector 14) now additionally read
  and print `CR2`, the actual faulting virtual address. That's the one
  piece of info a generic exception dump doesn't already have, and it's
  the one that actually matters for debugging a bad mapping.
- `kernel.c` — requests Limine's HHDM offset and kernel physical/virtual
  base addresses, calls `vmm_init()`, then deliberately dereferences an
  address that was never mapped (`0xdeadbeef000`) to prove the page
  fault handler works against our *own* tables, not Limine's.

Why HHDM mapping matters here: our own page-table-walking code
(`phys_to_virt()` in vmm.c, and the PMM's bitmap access) relies on being
able to turn a physical address into a dereferenceable virtual one. By
mapping the same HHDM offset Limine used into our own tables, all of
that code keeps working completely unchanged after the CR3 switch — we
didn't have to rewrite anything that came before this milestone.

Verified for real: booted in QEMU, watched `[ok] cr3 switched -- still
running` print (meaning code execution survived the switch to entirely
homemade page tables), watched a further serial print happen *after*
that switch (proving normal kernel operation continues, not just that
the switch instruction didn't immediately crash), then deliberately
touched an unmapped address and got back exactly `vector: 14 (Page
fault)` with `fault addr: 0xdeadbeef000` -- the CPU caught it and the
kernel printed a readable report instead of QEMU silently resetting.