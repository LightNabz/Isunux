bits 64

extern exception_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0      ; dummy error code, so every frame has the same shape
    push %1     ; interrupt vector number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1     ; interrupt vector number (CPU already pushed the error code)
    jmp isr_common
%endmacro

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; pass pointer to the saved-register frame as arg 1
    call exception_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16 ; drop vector number + error code
    iretq

; vectors that DON'T push a CPU error code get a dummy 0 pushed for us
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; ---- hardware IRQs, remapped by pic.c to vectors 32-47 ----
; these never carry a CPU-pushed error code, so it's always a dummy push.

extern irq_handler

%macro IRQ_STUB 1
global isr%1
isr%1:
    push 0
    push %1
    jmp irq_common
%endmacro

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call irq_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47

; ---- SYSCALL/SYSRET entry point ----
; Reached directly via IA32_LSTAR (set in gdt.c's syscall_init()), NOT
; through the IDT -- there's no vector, no idt_set_gate() call for this
; anywhere. SYSCALL hands us:
;   rcx = return RIP (the instruction after `syscall` in userland)
;   r11 = the user's RFLAGS at the moment of the call
;   rsp = still the USER stack -- unlike an interrupt gate, SYSCALL
;         does not switch stacks or push anything for us
; CS/SS are already fixed to the kernel selectors by hardware (from
; STAR), so it's safe to touch memory immediately -- just not the
; user's stack.
;
; Single-core means there's exactly one "current task's kernel stack"
; at any moment, and tss_set_kernel_stack() (task.c, every context
; switch) already keeps kernel_tss.rsp0 pointing at it -- the same
; field a real interrupt gate auto-loads from on ring3->0. Reading it
; directly here is what a multi-core kernel would need swapgs and a
; per-CPU scratch slot for; being single-core means a plain global
; works instead. Offset 4 is tss_t's layout (tss.h): a 4-byte
; reserved0 immediately followed by rsp0.

extern syscall_handler
extern kernel_tss

section .bss
syscall_scratch_user_rsp: resq 1

section .text
global syscall_entry
syscall_entry:
    mov [syscall_scratch_user_rsp], rsp
    mov rsp, [kernel_tss + 4]      ; kernel_tss.rsp0 -- this task's kernel stack top

    ; Manually rebuild the [rip,cs,rflags,rsp,ss] tail that an
    ; interrupt gate gets from hardware for free, in the exact same
    ; push order (and therefore the exact same interrupt_frame_t
    ; layout, idt.h) the old int-0x80 path relied on -- so fork.c's
    ; frame-copying trick needs no changes at all.
    push 0x23                          ; ss  = GDT_USER_DATA
    push qword [syscall_scratch_user_rsp] ; rsp = the user stack we just stashed
    push r11                           ; rflags -- SYSCALL put them here
    push 0x2b                          ; cs  = GDT_USER_CODE
    push rcx                           ; rip -- SYSCALL put the return address here

    push 0    ; dummy error code, same frame shape as every other stub
    push 128  ; purely informational now (no vector 128 exists anymore) -- nothing dispatches on this, syscall_handler reads frame->rax instead

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call syscall_handler

; Exported so a forked child's hand-built kernel stack (see fork.c) can
; skip straight to this exact point via switch_context's `ret` -- as far
; as this epilogue is concerned, it's popping a completely normal saved
; register frame either way, it has no idea (and doesn't need to know)
; whether syscall_handler actually just returned here for real, or
; whether a task is resuming for the very first time with a
; hand-crafted copy of its parent's frame sitting where a real one would be.
global syscall_return_point
syscall_return_point:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16 ; drop vector number + error code

    ; What's left on the stack is [rip,cs,rflags,rsp,ss], same order an
    ; iretq would consume -- except sysretq takes its return state from
    ; registers, not the stack, so pull each piece off by hand instead.
    pop rcx        ; rip    -> sysretq reads the return address from rcx
    add rsp, 8     ; cs     -> discarded; sysretq fixes CS from STAR, not the stack
    pop r11        ; rflags -> sysretq reads flags from r11
    pop rsp        ; rsp    -> restore the real user stack pointer -- safe to clobber rsp now, this is the last thing we need off the kernel stack
    ; the pushed ss value is simply abandoned on the old kernel stack --
    ; sysretq fixes SS from STAR too, the same way SYSCALL fixed it
    ; going in, so it was only ever here to keep the frame shape intact
    ; for fork.c, never actually consumed on the way out.

    o64 sysret

section .note.GNU-stack noalloc noexec nowrite progbits
