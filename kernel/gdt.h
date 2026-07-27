#pragma once

/* Segment selectors -- indices into the GDT built by gdt_init(), fixed
 * and referenced from other subsystems (the syscall gate, the ring-3
 * launch trampoline, etc). User selectors already have RPL=3 folded in
 * (the low 2 bits of a selector are the requested privilege level), so
 * these are ready to load into a segment register or drop straight into
 * an IRETQ frame as-is. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   (0x18 | 3)
#define GDT_USER_CODE   (0x20 | 3)
#define GDT_TSS         0x28

void gdt_init(void);
