#pragma once
#include <stdint.h>

/* Parses an ELF64 executable, maps each PT_LOAD segment into pml4_phys
 * with permissions matching that segment's own p_flags (real
 * per-segment read-only .text vs writable .data, unlike milestone 7
 * Group A's single-page flat binary), and writes:
 *   - the entry point to *entry_out
 *   - the page-aligned end of the highest-mapped segment to
 *     *highest_vaddr_out (a safe, unused-by-the-binary place to start
 *     a heap)
 *   - the runtime vaddr of the program header table itself to
 *     *phdr_vaddr_out, plus phentsize_out and phnum_out -- these three
 *     are AT_PHDR/AT_PHENT/AT_PHNUM for build_initial_stack()'s auxv.
 *     Computed as (the PT_LOAD segment covering file offset 0)'s
 *     p_vaddr + e_phoff -- true for every real ET_EXEC toolchain
 *     output (musl/gcc/etc always place the ELF header, and therefore
 *     the phdrs right after it, inside the first loaded segment), and
 *     exactly what a real kernel's binfmt_elf assumes too (there it's
 *     called load_bias + e_phoff, with load_bias 0 for a non-PIE
 *     ET_EXEC binary like this loader only supports).
 * Returns 1 on success, 0 on failure. */
int elf_load(uint64_t pml4_phys, const uint8_t *elf_data, uint64_t elf_size,
             uint64_t *entry_out, uint64_t *highest_vaddr_out,
             uint64_t *phdr_vaddr_out, uint16_t *phentsize_out, uint16_t *phnum_out);
