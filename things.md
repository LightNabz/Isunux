# Tier 0 — Correctness foundations (must fix before adding anything else) [DONE]

- ~~Free address spaces + task slots when a zombie is reaped~~ — done. `vmm_destroy_address_space()` walks and frees a whole address space; wired into `process_waitpid()` on reap and `do_exec()` on every exec (the latter was leaking on *every* exec, not just at reap time). Task kernel stacks now get freed on slot recycle too (`task_alloc_raw()`).
- ~~Remove hardcoded buffer ceilings~~ — done, plus the tmpfs write-side cap that was the same bug in disguise. `vfs_read_file_alloc()` replaces `exec_buf`/`init_elf_buf` with a growing PMM-backed buffer (no ceiling); `tmpfs_file_write()` now grows a file's backing storage on demand instead of silently truncating past 64KiB.
- ~~Copy-on-write `fork()`~~ — done. Added per-page refcounting to the PMM, a `PTE_COW` bit, and an actual page-fault handler in `exceptions.c` (there wasn't one before — every fault was fatal). `vmm_clone_lower_half()` now shares pages instead of deep-copying; a write triggers copy-or-reclaim in the fault handler.

# Tier 1 — The stuff that makes it a real multi-tasking system, not a demo [DONE]

- ~~Signals — `SIGINT` (Ctrl-C), `SIGKILL`, `SIGTERM`, `SIGCHLD` at minimum~~ — done. Without this, you can't kill a hung process from the shell or have the shell learn a child exited without polling `waitpid()` in a busy loop. This is probably the single biggest "feels like a toy" gap right now.
- ~~Job control — background (`&`), foreground, Ctrl-Z/suspend. Needs signals first~~ — done.
- ~~`mmap()` (even just anonymous) — heap growth beyond whatever fixed layout `exec()` currently sets up; `malloc()` in `mini_malloc.c` is presumably working off a fixed static arena right now, which caps every program's memory ceiling~~ — done.
- ~~User/group + permissions — no uid/gid, no file mode bits, everything is one implicit identity~~ — done. A Unix without permission enforcement doesn't feel like Unix even if it behaves like one when nobody's poking at it.

# Tier 2 — Persistence and real I/O (the "actually usable" tier) [DONE]

- ~~A block device driver (AHCI/ATA)~~ — done. Legacy ATA PIO, primary bus, master drive, LBA28, fully polled (no IRQ14 yet) — fixed ISA-compat ports (0x1F0-0x1F7/0x3F6), no PCI enumeration needed. `ata_init()`/`ata_present()`/`ata_sector_count()`/`ata_read_sectors()`/`ata_write_sectors()` in `kernel/ata.c`. Verified against real QEMU (`piix3-ide` + `ide-hd` on `-M q35`): IDENTIFY correctly reports a 16 MiB/32768-sector disk, and a write→read→byte-compare round-trip on sector 100 passes. `Makefile` grew a `disk.img` target (16 MiB, zero-filled, no prerequisites, never touched by `clean`) so persistence has something to persist to once the FAT layer lands.
- ~~A real filesystem on disk (FAT is the realistic first target)~~ — done, full read/write. `kernel/fat.c`/`fat.h`: whole-disk FAT16 (no MBR/partition table, matches `mkfs.fat -F 16` on a raw image), short 8.3 names only (LFN entries recognized and skipped on read; new entries always written in 8.3, truncated to fit, no LFN generation). Mounted at `/mnt` via `fat_install()` — same "ops-swap on a tmpfs dir" trick `devfs_install()` uses for `/dev`, but routed through `vnode_t::priv` instead of a direct ops-swap: FAT's ops need real per-node state (cluster, size), and swapping ops directly onto a tmpfs-allocated vnode would mean casting a `tmpfs_node_t*` as a `fat_node_t*` — memory corruption devfs never risks, since its ops ignore the vnode entirely. Two-step build, on purpose: (1) read + write-to-existing-files first (`fat_alloc_cluster`, `fat_write_fat_entry` replicated across both FAT copies, `fat_writeback_dirent` so growth persists past the in-memory vnode), verified with an `mcopy`'d file readable/writable/growable and surviving reboot; (2) `fat_dir_create`/`mkdir`/`unlink` (`fat_find_free_dirent_slot` reuses deleted entries or the current end-of-dir terminator without needing to relocate it, since the slot after a terminator is always already zeroed) as a deliberately separate step once (1) was proven, exactly because step (1) alone surfaced a real bug worth catching in isolation — the shell's `>` redirect fakes `O_TRUNC` via `unlink()`+`create()`, both of which were still `NULL` at that point, so it silently no-op'd and left old trailing bytes behind. Confirmed fixed and end-to-end persistent: `echo "..." > /mnt/test.txt` correctly truncates, and the new content is still there after a full reboot. Still `/dev`'s underlying problem too, though — this is still an ops-swap trick, not a real `mount()` concept; that's still open.
- ~~Persistence across reboot~~ — done, as of the FAT write support above. `echo ... > /mnt/<file>` survives a real QEMU restart, verified. Still scoped to `/mnt` only, not the whole root — `/bin`, `/tmp`, `/etc`, `/home` stay `tmpfs`/boot-module-seeded and reset every boot, which is fine (see the Tier 3 rootfs-refactor notes below, a separate concern from this).
- ~~A real console/tty layer — line discipline, canonical vs. raw mode, backspace/Ctrl-C handling at the terminal level rather than each program reimplementing it~~ — done.

# Tier 3 — Shell/userland maturity

- Linux static-binary syscall compatibility — renumber the syscall table to match Linux x86_64 ABI (`write`=1, `close`=3, `stat`=4, `open`=2, `pipe`=22, `dup2`=33, `unlink`=87, `brk`=12, `getcwd`=79, `chdir`=80, `mkdir`=83, `getdents64`=217 in place of `SYS_READDIR`, `fork`=57, `execve`=59, `wait4`=61 in place of `SYS_WAITPID`, `exit`=60/`exit_group`=231 in place of `SYS_EXIT`), plus add `mmap`=9, `arch_prctl`=158, `set_tid_address`=218, and `set_robust_list`=273 (the last two can be safe no-op stubs). Requires `mmap()` from Tier 1. Unlocks running real static-linked Linux binaries (coreutils, Lua, etc.) unmodified — `elf_load()` in `elf.c` already loads generic ELF64/x86_64 PT_LOAD segments and doesn't gate on `e_ident[EI_OSABI]`, so the loader side needs no changes. `mini_libc` and ISUNUX's own hand-written programs are unaffected — this only changes what number each syscall answers to, not any kernel internals. Do this while the syscall table is still small (~18 entries) — the same change gets expensive once more syscalls, `errno` conventions (Tier 4), or userland code hardcode the current numbers.
- Pipes and redirection actually wired into the shell (`|`, `>`, `<`) — `pipe.c` exists at the kernel level; whether `bin/sh/main.c` actually exposes shell syntax for it matters a lot for feel. Sequence alongside the syscall compat item above, not after — a borrowed `grep` binary is useless in the shell without pipes to feed it.
- Environment variables (`PATH`, `HOME`, etc.) and `execve()` actually searching `PATH` rather than requiring full paths.
- More coreutils depth — the current set (`ls`, `cat`, `echo`, `mkdir`, `pwd`, `touch`, `rm`) covers the absolute basics; things like `cp`, `mv`, `grep`, `ln` are what make it feel like you can actually do things rather than just poke at a demo. Becomes optional (rather than the only path) once Linux static-binary compat lands, since those utilities can then be borrowed as static-linked Linux binaries instead of hand-written against `mini_libc`.
### NOTE: 
- For coreutils, I'm actually thinking to use Toybox or BusyBox? This is because the first point of Tier 3 allowed us to do it.
- Also for how we pack them into ISO (for make iso/run/gui), I am thinking about how archiso from Arch Linux pack theirs:
```
Isunux.iso
 ├── EFI/ & boot/                             <-- [BOOTLOADER Compartment]
 ├── /boot/
 │    └── kernel.elf                          <-- [KERNEL]
 └── isunux/x86_64/
      └── airootfs.sfs (or .tar.gz, whatever) <-- [USERLAND ROOTFS]
```
- We can also create another make command perhaps? "make kernel" and "make rootfs"? "make kernel" will output kernel.elf and "make rootfs" will output the rootfs.tar.gz.

# Tier 4 — Polish that separates "cooked" from merely "functional"

- Proper error codes/`errno` equivalent returned consistently from syscalls, not just `-1`.
- Some kind of init system, even minimal — right now `/bin/sh` is hardcoded and manually bootstrapped in `kernel.c`; a real Unix has something that decides what runs first and respawns it (even a 20-line init counts).
- Config/rc file support for the shell (a `.shrc` equivalent) — small, but it's a very "this feels like a real system" signal.
- A test harness (mentioned earlier) — not user-facing, but without it every feature added above risks silently breaking what's already there, and regressions are what turn "well cooked" back into "held together with tape."

# Tier 5 — Legitimacy / completeness, not felt gaps

- User, Group, and permission enhancement.
- ASLR + PIE userland — I flagged this earlier as "fine to defer" (thankfully I did not forget this). Fixed `0x400000` load address is a real security smell but changes nothing about daily usability.
- A real time/clock subsystem — RTC read at boot, wall-clock time, `time()`/`gettimeofday()`-equivalent syscalls. Right now it's presumably PIT tick counting only; no program can know what day it is. Low effort, surprisingly high "feels like a real OS" payoff, so this might actually deserve to be higher — worth reconsidering.
- SMP / multicore support — the scheduler is almost certainly single-core right now (`task.c` cooperative/round-robin on one CPU). This is a legitimate "big kid OS" milestone but touches nearly everything (locking, per-CPU state, IPIs) — correctly a late-stage item.
- Swap / paging to disk — only becomes meaningful once there's a real disk (Tier 2) and real memory pressure. Pure legitimacy, no felt benefit on a toy workload.
- Dynamic linking / shared libraries — currently everything static-links `mini_libc`. Real ELF dynamic linking (`ld.so`-equivalent, PLT/GOT) is a substantial project on its own and mostly matters for binary size/toolchain maturity, not usability.
- Self-hosting — the "can it compile itself, running on itself" milestone that projects like ToaruOS treat as a badge of honor. Requires a working C toolchain ported to run inside ISUNUX (or cross-compiled to run there), which needs a filesystem, decent memory management, and enough syscalls to support something like a minimal `gcc`/`tcc`/`chibicc`. Huge, but iconic.
- Panic/debugging infrastructure — kernel backtraces on crash, maybe a minimal in-kernel debugger or at least symbol-aware panic output instead of just `hcf()` halting silently. This is invisible when things work and invaluable the moment Tier 1-3 work starts introducing real bugs — arguably should be pulled earlier, honestly, since it pays for itself the moment SMP or swap gets attempted.
- Syslog / kernel log ring buffer — right now everything goes straight to serial; a queryable in-kernel log (readable from userland via `/dev` or a syscall) is a small, classic "grown-up OS" feature.

# Tier Beyond

- Replace the blocky 8x16 font
- Add tiling terminal manager for multi-tasking. This also means we gotta implement PTY, hwhwhwhw
- Actual internet? ;3 -> Real talk: I actually read some book about network programming, maybe we'll borrow this from BSD derivatives, especially their TCP/IP thingy.
- Desktop Environment/Window Manager? gonna be pain in ass... I don't even continue my Wayland compositor project (vocwm) :v
- Let me know or just contribute!

# So what's my plan?
We gonna finish tier 0 to 2 first as it is the most fundamental part, then we're going to continue to the tier 3 where we're going to make our userland mature by also adopting Linux static-linked binary. Tier 4 and beyond will be just finishing I'd do in my free time later.

## Why did I create Isunux?

Let’s be real: reading the actual Linux kernel source is a nightmare if you just want to learn. You’re immediately hit with 30+ million lines of code, decades of legacy hardware hacks, dense macros, and enterprise abstractions. 

Isunux exists to be an **actual readable reference**. Instead of making "just another hobbyist Unix-like" that runs custom toy apps, the goal is to build a clean, well-commented x86_64 kernel that targets the real Linux ABI. 

Basically, it's a way to understand modern Linux plumbing and sys-calls without drowning in upstream bloat.
 
## Feedback
I'd very gladly accept feedback! as I myself is learning here.