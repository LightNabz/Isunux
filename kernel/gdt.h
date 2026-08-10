#pragma once

/* Segment selectors -- indices into the GDT built by gdt_init(), fixed
 * and referenced from other subsystems (the syscall entry stub, the
 * ring-3 launch trampoline, etc). User selectors already have RPL=3
 * folded in (the low 2 bits of a selector are the requested privilege
 * level), so these are ready to load into a segment register or drop
 * straight into an IRETQ frame as-is.
 *
 * The user segment ORDER here isn't arbitrary -- it's dictated by
 * SYSRET. Given STAR[63:48] = B, SYSRET loads SS from (B+8)|3 and CS
 * from (B+16)|3, which means it needs THREE consecutive slots
 * (unused/data/code), not two. 0x18 is that unused slot -- see
 * gdt.c's syscall_init() for the STAR value that points at it. */
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER32_CODE  0x18 /* never executed -- exists only so SYSRET's B+8/B+16 arithmetic lands on the right slots below */
#define GDT_USER_DATA    (0x20 | 3)
#define GDT_USER_CODE    (0x28 | 3)
#define GDT_TSS          0x30

void gdt_init(void);

/* Sets up SYSCALL/SYSRET (EFER.SCE + STAR/LSTAR/FMASK). Must be called
 * after gdt_init() -- see the doc comment on the definition. */
void syscall_init(void);
