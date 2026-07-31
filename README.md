# ISUNUX — x86_64

ISUNUX is a small, from-scratch x86_64 kernel project intended as a learning
and experimentation platform. It is freestanding, uses the Limine bootloader,
runs in 64-bit long mode, provides a basic userspace runtime and a small set
of kernel services (syscalls, a simple VFS, process creation, and an ELF
loader). The project is deliberately small and explicit: the goal is to
exercise the plumbing of a Unix-like kernel rather than to be a complete OS.

Key components
- `kernel/kernel.c` and related sources: boot protocol requests, early
  initialization, 16550 UART serial driver, basic console output and
  high-level kernel glue. See [kernel/kernel.c](kernel/kernel.c#L1).
- `kernel/linker.ld`: kernel linker script; places the kernel in the higher
  half and defines ELF layout used at link time. See [kernel/linker.ld](kernel/linker.ld#L1).
- `limine/` and `limine.conf`: pinned Limine bootloader binaries and the
  configuration used to produce a hybrid BIOS+UEFI bootable image.
- `kernel/*`: compact implementations of GDT/IDT, PIC, PIT, IRQ handling,
  a simple physical frame allocator (`pmm.c`), a basic virtual memory
  layout helper (`vmm.c`), ELF userland loader (`elf.c`), process and
  task bookkeeping, a tiny VFS and tmpfs, and syscall dispatch.
- `kernel/userprog/`: small userspace support and example programs. The
  userspace runtime includes `crt0.asm`, a tiny libc subset (`mini_*`), and
  several example programs (hello, sh, ls, cat, etc.).

Build and run (prerequisites)
- Host toolchain: `gcc`, `ld`, `nasm`.
- ISO tooling: `xorriso`.
- Emulator: `qemu-system-x86_64` (for running the ISO).
- `make` is used to orchestrate the build.

Common commands
```sh
make        # build kernel and embedded user programs
make iso    # build bootable ISO (iso_root/myos.iso)
make run    # build ISO and boot in QEMU headless (serial -> terminal)
make gui    # build ISO and boot in QEMU with visible window
make clean  # remove build artifacts
```

`make run` uses `-serial stdio -display none` so kernel serial output appears
directly in the terminal. `make gui` uses `-display sdl` but leaves serial on
stdio as well.

How userspace programs are built and embedded
- The top-level [Makefile](Makefile#L1) auto-discovers every directory under
  `bin/` as a userspace program.
- Each program `bin/<name>/main.c` is compiled with a userspace flagset and
  linked with the shared runtime (`lib/mini_*`) and `lib/crt0.o` using the
  userspace linker script [lib/user_link.ld](lib/user_link.ld#L1).
- The resulting ELF (`bin/<name>/<name>`) is a standalone executable that is
  staged onto the ISO at `iso_root/bin/<name>` and declared as a Limine boot
  module (never embedded in the kernel image itself).

Internals and ABI notes
- `lib/crt0.asm` is the userspace entrypoint; it expects the standard SysV
  stack layout and does a real `call main` so user programs should implement
  `int main(int argc, char **argv)`. See [lib/crt0.asm](lib/crt0.asm#L1).
- The userspace linker script places program segments starting at virtual
  address `0x400000` (see [lib/user_link.ld](lib/user_link.ld#L1)).
- The provided tiny runtime exposes a set of syscalls via `mini_libc.h`.
  Typical helpers include `sys_write`, `sys_read`, `sys_open`, `sys_execve`,
  `sys_fork`, `sys_waitpid`, `sys_exit`, `sys_readdir`, `sys_chdir`, and
  `sys_getcwd`. See [lib/mini_libc.h](lib/mini_libc.h#L1).
- On success, `main`'s return value is used as the process exit code.

How to add your own userspace program

1. Create a program directory and source file
   - Create a new directory `bin/<name>/`
   - Add a file `bin/<name>/main.c` that implements
     `int main(int argc, char **argv)`. Use the available runtime headers to
     call syscalls or the provided helpers:

     Example minimal program:

     ```c
     #include "mini_libc.h"
     #include "mini_printf.h"

     int main(int argc, char **argv) {
         printf("hello from %s\n", argv[0]);
         const char *msg = "direct sys_write example\n";
         sys_write(1, msg, 23);
         return 0;
     }
     ```

   - Do not provide your own `_start` symbol; `lib/crt0.asm` supplies the
     process entry and calls `main` with the correct `argc`/`argv` layout.

2. Build and run
   - No Makefile modifications are required. Simply run `make`. The Makefile
     will automatically discover your program under `bin/<name>/`, compile
     it with the shared userspace runtime, and stage it on the ISO at
     `iso_root/bin/<name>`.
   - To test your program, run `make run` and use the shell or other mechanisms
     the kernel provides to execute it.

Debugging and inspection
- Each userspace program is built as a standalone ELF at `bin/<name>/<name>`
  and can be inspected with `readelf` or `objdump` on the host. That is useful
  for checking symbol layout, entry point, and sections before it is staged
  on the ISO.

Limitations and notes
- This repository is a learning artifact: it intentionally implements a
  minimal set of features. Expect rough edges, intentionally minimal
  safety checks, and simplified implementations of many kernel subsystems.
- Userspace programs are statically linked with the tiny runtime provided in
  `lib/` and therefore do not require or use a full host libc.

Contributing and next steps
- If you add programs, consider adding basic tests or small example inputs
  so other contributors can exercise them.
- Future roadmap items include more complete interrupt handling, a
  full physical/virtual memory manager, improved scheduler behavior, and a
  richer VFS and userspace tooling set.

---

For quick reference, the essential files mentioned above are:
- [kernel/kernel.c](kernel/kernel.c#L1)
- [kernel/linker.ld](kernel/linker.ld#L1)
- [lib/crt0.asm](lib/crt0.asm#L1)
- [lib/user_link.ld](lib/user_link.ld#L1)
- [lib/mini_libc.h](lib/mini_libc.h#L1)
