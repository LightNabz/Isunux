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

; ---- syscall gate, vector 0x80 (128), triggered by `int 0x80` from ring 3 ----
; Entered from ring 3, so the CPU pushes 5 values (SS/RSP/RFLAGS/CS/RIP)
; instead of the 3 that every other stub above assumes -- but iretq
; figures out how many to pop again by looking at the CS value it's
; about to restore, so the exact same push-regs/call/pop-regs/iretq
; shape works completely unmodified. Routed to a different C function
; (syscall_handler, not exception_handler) purely to keep the two
; concerns separate.

extern syscall_handler

global isr128
isr128:
    push 0    ; dummy error code -- int 0x80 never carries one
    push 128  ; vector number
    jmp syscall_common

syscall_common:
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

section .note.GNU-stack noalloc noexec nowrite progbits
