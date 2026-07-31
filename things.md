# Tier 0 — Correctness foundations (must fix before adding anything else)

- Free address spaces + task slots when a zombie is reaped — right now every `fork()+exit()` cycle leaks memory forever. Adding features on top of a leaking kernel just means it dies faster.
- Remove hardcoded buffer ceilings (`init_elf_buf[65536]` and anything similar) — silent failure on oversized input isn't Unix-like, it's fragile.
- Copy-on-write `fork()` — not strictly required for correctness, but without it every `fork()+exec()` (the standard shell pattern) wastes full-memory copies, which will make the shell feel sluggish once more than a couple programs are chained.

# Tier 1 — The stuff that makes it a real multi-tasking system, not a demo

- Signals — `SIGINT` (Ctrl-C), `SIGKILL`, `SIGTERM`, `SIGCHLD` at minimum. Without this, you can't kill a hung process from the shell or have the shell learn a child exited without polling `waitpid()` in a busy loop. This is probably the single biggest "feels like a toy" gap right now.
- Job control — background (`&`), foreground, Ctrl-Z/suspend. Needs signals first.
- `mmap()` (even just anonymous) — heap growth beyond whatever fixed layout `exec()` currently sets up; `malloc()` in `mini_malloc.c` is presumably working off a fixed static arena right now, which caps every program's memory ceiling.
- User/group + permissions — no uid/gid, no file mode bits, everything is one implicit identity. A Unix without permission enforcement doesn't feel like Unix even if it behaves like one when nobody's poking at it.

# Tier 2 — Persistence and real I/O (the "actually usable" tier)

- A block device driver (AHCI/ATA) — currently zero disk I/O exists; everything is RAM-resident and boot-module-seeded.
- A real filesystem on disk (FAT is the realistic first target) + a real `mount()` — right now `/dev` is a hack (ops-table swap on a `tmpfs` dir), not a real mount concept.
- Persistence across reboot — nothing you do in the shell survives a reboot, since it's `tmpfs`-only. This is probably the most emotionally significant missing piece — "I edited a file and it's still there after rebooting" is when an OS stops feeling like a simulation.
- A real console/tty layer — line discipline, canonical vs. raw mode, backspace/Ctrl-C handling at the terminal level rather than each program reimplementing it.

# Tier 3 — Shell/userland maturity

- Pipes and redirection actually wired into the shell (`|`, `>`, `<`) — `pipe.c` exists at the kernel level; whether `bin/sh/main.c` actually exposes shell syntax for it matters a lot for feel.
- Environment variables (`PATH`, `HOME`, etc.) and `execve()` actually searching `PATH` rather than requiring full paths.
- More coreutils depth — the current set (`ls`, `cat`, `echo`, `mkdir`, `pwd`, `touch`, `rm`) covers the absolute basics; things like `cp`, `mv`, `grep`, `ln` are what make it feel like you can actually do things rather than just poke at a demo.

# Tier 4 — Polish that separates "cooked" from merely "functional"

- Proper error codes/`errno` equivalent returned consistently from syscalls, not just `-1`.
- Some kind of init system, even minimal — right now `/bin/sh` is hardcoded and manually bootstrapped in `kernel.c`; a real Unix has something that decides what runs first and respawns it (even a 20-line init counts).
- Config/rc file support for the shell (a `.shrc` equivalent) — small, but it's a very "this feels like a real system" signal.
- A test harness (mentioned earlier) — not user-facing, but without it every feature added above risks silently breaking what's already there, and regressions are what turn "well cooked" back into "held together with tape."

# Tier 5 — Legitimacy / completeness, not felt gaps

- User, Group, and permission — it is not quite Unix if we don't have this feature.
- ASLR + PIE userland — I flagged this earlier as "fine to defer" (thankfully I did not forget this). Fixed `0x400000` load address is a real security smell but changes nothing about daily usability.
- A real time/clock subsystem — RTC read at boot, wall-clock time, `time()`/`gettimeofday()`-equivalent syscalls. Right now it's presumably PIT tick counting only; no program can know what day it is. Low effort, surprisingly high "feels like a real OS" payoff, so this might actually deserve to be higher — worth reconsidering.
- SMP / multicore support — the scheduler is almost certainly single-core right now (`task.c` cooperative/round-robin on one CPU). This is a legitimate "big kid OS" milestone but touches nearly everything (locking, per-CPU state, IPIs) — correctly a late-stage item.
- Swap / paging to disk — only becomes meaningful once there's a real disk (Tier 2) and real memory pressure. Pure legitimacy, no felt benefit on a toy workload.
- Dynamic linking / shared libraries — currently everything static-links `mini_libc`. Real ELF dynamic linking (`ld.so`-equivalent, PLT/GOT) is a substantial project on its own and mostly matters for binary size/toolchain maturity, not usability.
- Self-hosting — the "can it compile itself, running on itself" milestone that projects like ToaruOS treat as a badge of honor. Requires a working C toolchain ported to run inside ISUNUX (or cross-compiled to run there), which needs a filesystem, decent memory management, and enough syscalls to support something like a minimal `gcc`/`tcc`/`chibicc`. Huge, but iconic.
- Panic/debugging infrastructure — kernel backtraces on crash, maybe a minimal in-kernel debugger or at least symbol-aware panic output instead of just `hcf()` halting silently. This is invisible when things work and invaluable the moment Tier 1-3 work starts introducing real bugs — arguably should be pulled earlier, honestly, since it pays for itself the moment SMP or swap gets attempted.
- Syslog / kernel log ring buffer — right now everything goes straight to serial; a queryable in-kernel log (readable from userland via `/dev` or a syscall) is a small, classic "grown-up OS" feature.

## Tier Beyond

- Replace the blocky 8x16 font
- Add tiling terminal manager for multi-tasking
- Actual internet? ;3