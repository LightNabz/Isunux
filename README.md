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
- The top-level [Makefile](Makefile#L1) compiles kernel sources and also
  compiles each userspace program placed in `kernel/userprog/`.
- Each program `NAME.c` in `kernel/userprog` is compiled with a userspace
  flagset and linked with the shared runtime (`mini_*`) and `crt0.o` using
  the userspace linker script [kernel/userprog/user_link.ld](kernel/userprog/user_link.ld#L1).
- The build then generates a tiny assembly wrapper that `incbin`s the linked
  ELF into the kernel image; that wrapper is assembled and linked into the
  kernel so the kernel can instantiate the program image at runtime.

Internals and ABI notes
- `crt0.asm` (in `kernel/userprog`) is the userspace entrypoint; it
  expects the standard SysV stack layout and does a real `call main` so
  user programs should implement `int main(int argc, char **argv)`.
  See [kernel/userprog/crt0.asm](kernel/userprog/crt0.asm#L1).
- The userspace linker script places program segments starting at virtual
  address `0x400000` (see [kernel/userprog/user_link.ld](kernel/userprog/user_link.ld#L1)).
- The provided tiny runtime exposes a set of syscalls via `mini_libc.h`.
  Typical helpers include `sys_write`, `sys_read`, `sys_open`, `sys_execve`,
  `sys_fork`, `sys_waitpid`, `sys_exit`, `sys_readdir`, `sys_chdir`, and
  `sys_getcwd`. See [kernel/userprog/mini_libc.h](kernel/userprog/mini_libc.h#L1).
- On success, `main`'s return value is used as the process exit code.

How to add your own userspace program

1. Create the program source
   - Add a new file `kernel/userprog/<name>.c` that implements
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

   - Do not provide your own `_start` symbol; `crt0.asm` supplies the
     process entry and calls `main` with the correct `argc`/`argv` layout.

2. Expose the program to the build
   - Open the top-level [Makefile](Makefile#L1) and add your program name to
     the `USER_PROGRAMS` variable. For example, to add `myprog`:

     ```make
     USER_PROGRAMS := hello sh echo cat ls myprog
     ```

   - The Makefile rules will then:
     - compile `kernel/userprog/myprog.c` with the userspace flags,
     - link `kernel/userprog/myprog_elf` (program ELF) against the shared
       runtime and `crt0.o` using `kernel/userprog/user_link.ld`,
     - generate `kernel/userprog/myprog_blob.asm` containing an `incbin` of
       the linked ELF, assemble it to `myprog_blob.o`, and link that object
       into the kernel image.

3. Build and run
   - Run `make`. The kernel binary will include your program and it will be
     available in the kernel's in-memory userspace image and in the VFS
     layout produced by the build if the kernel places it under `/bin`.
   - To test the program's behavior you can run `make run` and use the shell
     or other mechanisms the kernel provides to execute it.

Debugging and inspection
- The intermediate `kernel/userprog/<name>_elf` is a standard ELF file and
  can be inspected with `readelf` or `objdump` on the host. That is useful
  for checking symbol layout, entry point, and sections before it is
  embedded in the kernel image.

Limitations and notes
- This repository is a learning artifact: it intentionally implements a
  minimal set of features. Expect rough edges, intentionally minimal
  safety checks, and simplified implementations of many kernel subsystems.
- Userspace programs are statically linked with the tiny runtime provided in
  `kernel/userprog/*` and therefore do not require or use a full host libc.

Contributing and next steps
- If you add programs, consider adding basic tests or small example inputs
  under `kernel/userprog/` so other contributors can exercise them.
- Future roadmap items include more complete interrupt handling, a
  full physical/virtual memory manager, improved scheduler behavior, and a
  richer VFS and userspace tooling set.

---

For quick reference, the essential files mentioned above are:
- [kernel/kernel.c](kernel/kernel.c#L1)
- [kernel/linker.ld](kernel/linker.ld#L1)
- [kernel/userprog/crt0.asm](kernel/userprog/crt0.asm#L1)
- [kernel/userprog/user_link.ld](kernel/userprog/user_link.ld#L1)
- [kernel/userprog/mini_libc.h](kernel/userprog/mini_libc.h#L1)
