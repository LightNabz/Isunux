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

## Milestone 7

- `kernel/vfs.c` / `vfs.h` -- the actual "virtual" part of the VFS: a
  `vnode_ops_t` table (`read`/`write`/`lookup` function pointers) that
  any filesystem implementation fills in, plus `vfs_resolve_path()`
  which walks an absolute path one component at a time via each
  directory's `lookup`. The syscall layer only ever talks to this
  interface, never to tmpfs directly -- a real disk-backed filesystem
  can be dropped in later without touching a single syscall.
- `kernel/tmpfs.c` / `tmpfs.h` -- an in-memory filesystem tree. Nodes
  come from a static pool (no heap yet), file content lives in a
  PMM-allocated page accessed through HHDM, directories are a simple
  linked list of children. `tmpfs_node_t` embeds a `vnode_t` as its
  first member specifically so a `vnode_t*` and a `tmpfs_node_t*` are
  pointer-compatible -- the standard C "poor man's inheritance" trick.
- `kernel/process.c` / `process.h` -- `process_t` owns a `pml4_phys` and
  a 16-entry fd table. fd 0/1/2 point at a **console vnode** whose
  `write` op just calls `serial_putc` directly -- this is what lets
  `SYS_WRITE` stop being hardcoded to serial and become genuinely
  "write to whatever fd you were given." `process_open/read/write/close`
  are the only things that touch the fd table; the syscall dispatcher
  never does.
- `kernel/syscall.c` -- `SYS_WRITE` generalized to `(fd, buf, len)`
  routed through `process_write`, plus new `SYS_OPEN`, `SYS_READ`,
  `SYS_CLOSE`.
- `kernel/userprog/hello.asm` -- upgraded from milestone 6's
  write-then-exit into a real test: opens `/hello.txt`, reads it, writes
  what came back to stdout, closes the fd, exits. Proves the round trip
  with real data, not just a syscall that returns successfully.

Verified for real, and it caught an actual bug worth noting: the first
attempt page-faulted (vector 14, error code `0x7` -- a *write* to a
*present, user, read-only* page) because the flat binary's code and its
writable scratch variables (`buf`, `fd_val`) share a single page, and
that page was mapped without `PTE_WRITE` as a nod to milestone 6's
"code should be read-only" idea. Without a real ELF loader there's no
program-header information to tell code and data apart, so there's no
honest way to map them with different permissions yet -- the fix was to
mark the page writable and note plainly that per-segment W^X-style
permissions are a Group B (ELF loader) deliverable, not a Group A one.

Full run after the fix: the program opened `/hello.txt`, read the exact
51 bytes `vfs_init()` had seeded into it, wrote that content back out
through the console vnode, closed the fd, and exited -- open, read,
write, and close all going through the same `process_t` -> `vfs` ->
`tmpfs` chain that a real program would use.

- `kernel/elf.c` / `elf.h` -- a minimal ELF64 loader. Validates the
  magic/class/machine fields, walks the program header table, and for
  every `PT_LOAD` segment: allocates physically contiguous pages, copies
  `p_filesz` bytes from the file (the gap up to `p_memsz` is `.bss` --
  zeroed for free since the pages come pre-zeroed), and maps it with
  **real permissions taken from that segment's own `p_flags`** --
  `PTE_WRITE` only if `PF_W` is actually set. This is the fix for the
  exact gap Group A ran into: without ELF section info there was no
  honest way to tell code from data, so everything had to be mapped
  writable. Now there's real per-segment W^X-ish enforcement.
- `kernel/userprog/mini_libc.h` -- the tiny syscall-wrapper "libc":
  `sys_write`/`sys_open`/`sys_read`/`sys_close`/`sys_exit`, each just an
  inline-asm `int 0x80` with the right registers set up. Enough to write
  a real C program against our syscall ABI with zero actual libc.
- `kernel/userprog/hello.c` -- **hello.asm is retired.** Same VFS test
  as Group A (open `/hello.txt`, read it, write it back, close, exit),
  but now genuine compiled C with a real `_start`, no hand-written
  assembly anywhere in the test program itself.
- `kernel/userprog/user_link.ld` -- a small linker script placing
  userspace programs at `0x400000`, with `.text`/`.rodata` in one
  `PT_LOAD` (R+X) and `.data`/`.bss` in a separate one (R+W) -- this is
  what makes real per-segment permissions possible on the loader side.
- `kernel.c` -- the manual "copy bytes into a page, map one page"
  dance from Group A is gone, replaced by a single `elf_load()` call
  that derives both the mappings *and* the entry point (`e_entry`) from
  the ELF itself, rather than a hardcoded `0x400000` constant.

Caught one real thing along the way: the linker emitted a second,
**empty** `PT_LOAD` segment (`p_memsz == 0`) for the unused `.data`/
`.bss` output sections, since `hello.c` has no actual global mutable
state (its scratch buffer is a stack-local array). `elf_load()` now
explicitly skips zero-size segments -- without that check,
`pmm_alloc_pages(0)` returning 0 would've been misread as "out of
memory" and failed the whole load.

Verified for real: `readelf -l` on the built executable confirms real
program headers (one `R E` segment at `0x400000`), and the boot log
shows the loader reporting `perms r-x` for it -- genuinely read-only,
executable code, not "writable because we had no way to know
otherwise." The VFS round trip (open/read/write/close against
`/hello.txt`) still works identically, now driven entirely by compiled
C through the mini-libc.

ISUNUX can now load and
run genuine compiled programs, with real per-segment memory permissions,
that talk to a real (if simple) filesystem through a real syscall ABI.
That's the "userspace bridge" phase's filesystem leg closed out.

## Milestone 8

- `SYS_BRK` -- new syscall. `process_t` gained `heap_start`/`heap_end`
  fields; `heap_start` is derived automatically from `elf_load()`'s new
  `highest_vaddr_out` parameter (one page past the highest `PT_LOAD`
  segment), not guessed or hardcoded.
- `process_brk()` -- classic semantics: `new_brk == 0` queries the
  current break without changing anything; otherwise it maps fresh
  pages up to the requested break and returns the new value. No
  shrinking support yet (a smaller request is a silent no-op) -- same
  scope cut the PMM made with `pmm_free_page` never returning memory to
  the OS.
- `kernel/userprog/mini_malloc.c` -- a genuinely working first-fit
  free-list heap allocator, backed entirely by `sys_brk()`. Each
  allocation is a small header (`size`/`free`/`next`) plus payload;
  `malloc()` walks existing blocks looking for a free one big enough
  before ever asking the kernel for more memory, and `free()` just
  flips a flag (no coalescing yet, but reuse of a same-size block works
  correctly without it).
- `mini_libc.h` gained `sys_brk()`.

Verified for real, and it happened to catch something nice: this is the
first userspace program with actual global mutable state (`mini_malloc`'s
two static pointers), so for the first time the linker produced a
**genuinely non-empty** `.data` segment alongside `.text` -- and the
loader correctly mapped it `rw-` versus the code's `r-x`, real evidence
the per-segment permission system built in Group B matters and works,
not just an edge case that happened to stay empty.

Full run: the VFS test from milestone 7 still passes unchanged, then
`malloc(64)` pulled a fresh page via `brk` growing from `0x402000`
(computed, not hardcoded), a string was written directly into that
allocated memory and printed back correctly, and after `free()` a
second same-size `malloc()` call returned the *exact same pointer* --
proof the free list is really being searched and really being reused,
not just leaking forward forever.

- `kernel/userprog/mini_string.c` -- real `strlen`/`strcmp`/`memcpy`/
  `memset`, matching the actual standard signatures exactly (so GCC's
  builtin-declaration checking has nothing to conflict with). The old
  makeshift `strlen_` in `mini_libc.h` is gone now that a real one exists.
- `kernel/userprog/mini_printf.c` -- `printf` supporting `%d`/`%s`/`%x`/
  `%c`/`%%`. Formats into a fixed 256-byte buffer, then flushes with one
  `sys_write` -- simple, but genuinely correct for the format specifiers
  it supports.
- `kernel/userstack.c` / `userstack.h` -- `build_initial_stack()`, which
  writes argc/argv/envp/auxv near the top of a fresh user stack exactly
  how a real SysV-ABI crt0 expects to find them: argv strings first
  (growing downward), then the pointer arrays (argc, argv[], NULL,
  envp's NULL terminator, then a minimal auxv --  `AT_PAGESZ`/4096 and
  the `AT_NULL` terminator pair), 16-byte-aligning the final result
  since that's what the ABI requires at process entry. Returns the
  resulting initial RSP -- no longer just the raw stack top constant.
- `kernel/userprog/crt0.asm` -- the real entry point now. Reads
  `argc`/`argv` straight off `[rsp]`/`[rsp+8]`, computes `envp` as
  `argv + (argc+1)*8`, and makes a genuine `call main` (not a jump) so
  `main`'s own compiler-generated prologue sees exactly the stack state
  any normal function call would produce.
- `kernel/userprog/hello.c` -- rewritten around a real
  `int main(int argc, char **argv)`. No hand-wrapped `_start` in the
  test program itself anymore; that's entirely `crt0.asm`'s job now,
  same separation of concerns any real C program has.

Verified for real: `argc = 1` / `argv[0] = hello` printed correctly --
genuine argument parsing, not hardcoded -- and the VFS + heap tests from
Groups A/A both still pass, now driven entirely through `printf` instead
of raw `sys_write` calls.

## Milestone 8: fully complete

`brk`-backed heap allocation with real `malloc`/`free` reuse (Group A)
plus `printf`, `string.h`, and a genuine ABI-compliant process entry
with real `argc`/`argv` (Group B). ISUNUX user programs now look and
behave like actual normal C programs -- dynamic memory, formatted
output, real `main()` -- built entirely on ISUNUX's own tiny libc rather
than a ported one.

## Next: Milestone 9, shell + coreutils

The last milestone on the original roadmap. Rough shape: a simple shell
program (read a line from stdin, parse it, `fork`+`exec` or just
directly dispatch built-in commands, repeat) and a handful of coreutils
(`ls`, `cat`, `echo` at minimum) as separate ELF executables the shell
can load via the ELF loader we already have. This will need real stdin
(the keyboard IRQ exists since milestone 2 but nothing's ever piped its
scancodes anywhere useful yet -- console reads have just returned EOF)
and very likely `fork`/`exec` as real syscalls, which is a meaningfully
bigger step than anything in milestone 8: `fork` means cloning an
entire address space, not just building one from scratch.