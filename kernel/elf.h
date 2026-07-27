#pragma once
#include <stdint.h>

/* Parses an ELF64 executable, maps each PT_LOAD segment into pml4_phys
 * with permissions matching that segment's own p_flags (real
 * per-segment read-only .text vs writable .data, unlike milestone 7
 * Group A's single-page flat binary), and writes the entry point to
 * *entry_out and the page-aligned end of the highest-mapped segment to
 * *highest_vaddr_out (a safe, unused-by-the-binary place to start a
 * heap). Returns 1 on success, 0 on failure. */
int elf_load(uint64_t pml4_phys, const uint8_t *elf_data, uint64_t elf_size,
             uint64_t *entry_out, uint64_t *highest_vaddr_out);
