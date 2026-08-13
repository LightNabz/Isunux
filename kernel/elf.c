#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "kutil.h"
#include "serial.h"

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define ELFCLASS64  2
#define EM_X86_64   0x3e

int elf_load(uint64_t pml4_phys, const uint8_t *elf_data, uint64_t elf_size,
             uint64_t *entry_out, uint64_t *highest_vaddr_out,
             uint64_t *phdr_vaddr_out, uint16_t *phentsize_out, uint16_t *phnum_out) {
    if (elf_size < sizeof(elf64_ehdr_t)) {
        serial_print("[elf] file too small to even hold a header\n");
        return 0;
    }

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)elf_data;

    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        serial_print("[elf] bad magic -- not an ELF file\n");
        return 0;
    }
    if (eh->e_ident[4] != ELFCLASS64) {
        serial_print("[elf] not a 64-bit ELF\n");
        return 0;
    }
    if (eh->e_machine != EM_X86_64) {
        serial_print("[elf] not x86_64\n");
        return 0;
    }

    uint64_t phdr_table_end = eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (phdr_table_end > elf_size) {
        serial_print("[elf] program header table runs past end of file\n");
        return 0;
    }

    uint64_t hhdm = vmm_hhdm_offset();
    uint64_t highest_end = 0;
    uint64_t phdr_vaddr = 0; /* filled in once we hit the segment covering file offset 0 */

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph =
            (const elf64_phdr_t *)(elf_data + eh->e_phoff + (uint64_t)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue; /* an empty segment (e.g. unused .data/.bss) has nothing to map */

        /* the segment starting at file offset 0 is the one carrying the
         * ELF header (and, right after it, the phdr table itself) --
         * see the doc comment on elf_load() in elf.h for why this
         * arithmetic is correct */
        if (ph->p_offset == 0) phdr_vaddr = ph->p_vaddr + eh->e_phoff;

        /* segment might not start on a page boundary, and its in-memory
         * size (p_memsz) can be bigger than its on-disk size (p_filesz)
         * -- that gap is .bss, which needs to exist as zeroed memory but
         * isn't actually stored in the file */
        uint64_t seg_start = ph->p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t seg_end = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t offset_in_first_page = ph->p_vaddr - seg_start;
        uint64_t num_pages = (seg_end - seg_start) / PAGE_SIZE;

        uint64_t phys = pmm_alloc_pages(num_pages);
        if (phys == 0) {
            serial_print("[elf] out of memory loading a segment\n");
            return 0;
        }

        uint8_t *dst = (uint8_t *)(hhdm + phys);
        k_memset(dst, 0, num_pages * PAGE_SIZE); /* zeroes the .bss tail for free */
        for (uint64_t b = 0; b < ph->p_filesz; b++) {
            dst[offset_in_first_page + b] = elf_data[ph->p_offset + b];
        }

        /* the actual point of doing this properly: permissions come
         * from the segment itself, not a one-size-fits-all guess */
        uint64_t flags = PTE_USER;
        if (ph->p_flags & PF_W) flags |= PTE_WRITE;

        for (uint64_t p = 0; p < num_pages; p++) {
            vmm_map_4k_in(pml4_phys, seg_start + p * PAGE_SIZE,
                          phys + p * PAGE_SIZE, flags);
        }

        if (seg_end > highest_end) highest_end = seg_end;

        serial_print("[elf] segment vaddr ");
        serial_print_hex(ph->p_vaddr);
        serial_print(" memsz ");
        serial_print_dec(ph->p_memsz);
        serial_print(" bytes, perms ");
        serial_print(ph->p_flags & PF_R ? "r" : "-");
        serial_print(ph->p_flags & PF_W ? "w" : "-");
        serial_print(ph->p_flags & PF_X ? "x" : "-");
        serial_print("\n");
    }

    *entry_out = eh->e_entry;
    *highest_vaddr_out = highest_end;
    *phdr_vaddr_out = phdr_vaddr;
    *phentsize_out = eh->e_phentsize;
    *phnum_out = eh->e_phnum;
    return 1;
}
