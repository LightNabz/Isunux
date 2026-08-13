#pragma once
#include <stdint.h>

/* Writes argc/argv/envp/auxv near the top of a process's stack, laid
 * out exactly how a real SysV-ABI crt0 (ours, or a borrowed static
 * musl/glibc one) expects to find them, and returns the resulting
 * initial RSP (a user virtual address, 16-byte aligned) to hand to
 * enter_userspace(). Strings and pointer arrays are both written into
 * the *top* of the stack region; everything below the returned RSP is
 * left free for the program's actual runtime stack growth.
 *
 * entry_point/phdr_vaddr/phentsize/phnum come straight from elf_load()
 * -- see its doc comment in elf.h for where phdr_vaddr's arithmetic
 * comes from. They only feed AT_ENTRY/AT_PHDR/AT_PHENT/AT_PHNUM below;
 * mini_libc's own crt0.asm never reads the auxv at all (it only wants
 * argc/argv/envp), but a real musl/glibc _start does, immediately --
 * musl's in particular dereferences AT_RANDOM before main() ever runs,
 * for its stack-protector canary. Skip any of these and a borrowed
 * static binary segfaults before you'd get a chance to see why. */
uint64_t build_initial_stack(uint64_t hhdm_offset,
                              uint64_t stack_phys,
                              uint64_t stack_base_vaddr,
                              uint64_t stack_size,
                              int argc,
                              const char **argv,
                              int envc,
                              const char **envp,
                              uint64_t entry_point,
                              uint64_t phdr_vaddr,
                              uint16_t phentsize,
                              uint16_t phnum);
