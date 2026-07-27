#pragma once
#include <stdint.h>

/* Writes argc/argv/envp/auxv near the top of a process's stack, laid
 * out exactly how a real SysV-ABI crt0 expects to find them, and
 * returns the resulting initial RSP (a user virtual address, 16-byte
 * aligned) to hand to enter_userspace(). Strings and pointer arrays are
 * both written into the *top* of the stack region; everything below
 * the returned RSP is left free for the program's actual runtime stack
 * growth. */
uint64_t build_initial_stack(uint64_t hhdm_offset,
                              uint64_t stack_phys,
                              uint64_t stack_base_vaddr,
                              uint64_t stack_size,
                              int argc,
                              const char **argv);
