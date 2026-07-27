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

## Milestone 3: physical memory manager

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

## Milestone 4: virtual memory / paging

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

## Milestone 5

- `kernel/switch.asm` -- `switch_context(old_rsp_ptr, new_rsp)`. Pushes
  the 6 callee-saved registers onto the outgoing task's stack, stashes
  the resulting `rsp` into `*old_rsp_ptr`, loads the incoming task's
  `rsp`, pops its 6 callee-saved registers back off, and `ret`s. That
  `ret` is the whole trick -- it jumps to whatever address is sitting on
  top of the *new* stack.
- `kernel/task.c` / `task.h` -- the task manager. `task_t` is a saved
  `rsp` + a stack + a name + a `next` pointer (tasks live in a circular
  linked list, drawn from a fixed static pool since there's no heap
  yet). `pmm_alloc_pages()` (new in pmm.c) grabs a contiguous run of
  physical pages for each task's stack.
- The hand-crafted "fake initial frame": `task_create()` doesn't run the
  new task, it *pre-writes* its stack to look exactly like it already
  went through one `switch_context` call -- 6 zeroed callee-saved
  registers plus a fake return address pointing at
  `task_entry_trampoline`, plus one padding qword so the trampoline
  lands with ABI-correct 16-byte stack alignment. This means
  `switch_context` never has to know or care whether it's resuming a
  task or launching one for the first time -- the `ret` just works
  either way.
- `yield()` -- marks the current task `READY`, walks the ring to find
  the next `READY` task, and calls `switch_context`. Pure round-robin,
  no priorities, no timer involved yet -- purely voluntary.
- `kernel.c` -- creates two test tasks that each loop 3 times printing
  and calling `yield()`, then `main` (the original boot flow, now task 0
  in the ring) calls `yield()` 8 times in a row.

Verified for real: booted in QEMU and watched task-a and task-b cleanly
interleave -- `task-b 0`, `task-a 0`, `task-b 1`, `task-a 1`, `task-b 2`,
`task-a 2`, then both finish and retire through the trampoline. That's
two independent, separately-stacked flows of execution correctly taking
turns and resuming exactly where each left off, driven entirely by
`switch_context`.

- `kernel/irq.c` -- restructured so EOI is sent immediately per-branch,
  *before* any possible context switch (a switch might not return to
  that exact call site for a while, and the PIC needs the EOI promptly
  regardless). Every `TIME_SLICE_TICKS` (5 ticks = 50ms at our 100Hz
  PIT), the timer branch calls `yield()` directly from inside the
  interrupt handler.
- `kernel/task.c` -- `task_entry_trampoline` now starts every fresh task
  with `sti`. This matters specifically for tasks that get first
  scheduled via preemption rather than a voluntary yield: hardware
  auto-clears the interrupt flag on ISR entry, and without this fix a
  task launched that way would silently start with interrupts disabled
  forever.
- `kernel.c` -- the test changed shape entirely from milestone 5's first
  half: three tasks (`task-x/y/z`), each an infinite loop with a
  busy-spin between prints, **none of which ever call `yield()`**. Main
  itself just halts in a loop (interrupts still enabled) instead of
  explicitly cooperating either.

The key realization that makes this work with almost no new code: `yield()`
doesn't know or care whether it was called voluntarily from normal code
or involuntarily from inside `irq_handler`. The call chain
(`irq_common` asm -> `irq_handler` -> `yield` -> `switch_context`)
unwinds itself naturally through ordinary `call`/`ret` semantics --
when a preempted task is resumed, `switch_context`'s `ret` walks back up
through `yield`'s return, then `irq_handler`'s return, and lands back in
`irq_common`'s epilogue, which pops that task's saved general-purpose
registers and executes the real `iretq` -- resuming the task exactly
where the timer originally interrupted it, flags and all. No special
"am I in an interrupt" case needed anywhere.

Verified for real: booted in QEMU, watched three tasks with zero
cooperation between them get cleanly round-robined by the timer alone --
beat counts across all three stayed within 1 of each other over a 6
second run (fair, no starvation), occasional out-of-order interleaving
(`x, z, y` instead of the "expected" `z, y, x`) confirming this is
genuine preemption timing and not a hardcoded sequence, and the
once-a-second uptime line interleaving cleanly throughout without
disrupting anything.

## Project renamed: ISUNUX

"Isurus not Unix" -- Isurus being the mako shark genus, continuing the
grand Unix wordplay tradition. Same project, same code, just naming it
properly now that it's got real teeth.

## Milestone 6

- `kernel/tss.c` / `tss.h` -- the Task State Segment. In long mode we
  only actually use one field, `rsp0`: the kernel stack the CPU
  auto-loads the instant a ring-3 program traps into ring 0.
  `iomap_base` is deliberately set to `sizeof(tss_t)` -- past the
  segment limit -- meaning there's no I/O permission bitmap at all, so
  every port access from ring 3 gets rejected by default.
- `kernel/gdt.c` -- extended from 3 entries to 7: null, kernel
  code/data (unchanged), new user code/data (DPL=3, so ring 3 is allowed
  to load them), and a 16-byte TSS descriptor (needs two GDT slots
  instead of one, since it carries a full 64-bit base address). `gdt.h`
  now exposes the selector constants (`GDT_KERNEL_CODE`,
  `GDT_USER_CODE`, etc.) for other subsystems to reference -- the user
  ones already have RPL=3 folded into the low bits, ready to drop
  straight into a segment register or an IRETQ frame.
- `kernel/vmm.c` -- generalized from "always operates on the kernel's
  one PML4" to "operates on whichever PML4 you pass it"
  (`vmm_map_4k_in`), plus a new `PTE_USER` flag. Intermediate page table
  entries (PML4/PDPT/PD) are now always marked user-accessible --
  that's safe by itself since x86 paging ANDs the U/S bit down the
  whole walk, so real protection still lives entirely in the leaf
  entry's own flags.
- `vmm_new_address_space()` -- allocates a fresh PML4, copies the
  kernel's top 256 entries (indices 256-511, canonical high half --
  where the HHDM window and kernel image live) so every address space
  automatically shares the same kernel mappings, and leaves the bottom
  256 entries (canonical low half) completely empty for a process to
  map its own code/stack into later without touching anyone else's.
- `vmm_activate()` -- just a named wrapper around loading CR3, so
  switching address spaces reads clearly at call sites.

Verified for real: booted in QEMU and confirmed every selector value,
confirmed the TSS's `rsp0` setter actually updates the struct, then
built a brand new address space and checked it byte-for-byte -- low
half entirely zero, high half identical to the kernel's own PML4 -- and
finally switched CR3 to that new (95% empty) address space and *back*,
watching the kernel keep running the whole time because the shared
higher half kept the ground under it solid.