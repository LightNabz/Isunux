bits 64

; void switch_context(uint64_t *old_rsp_ptr, uint64_t new_rsp)
; rdi = pointer to where we save the outgoing task's rsp
; rsi = the incoming task's rsp value to load
global switch_context
switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp   ; stash outgoing task's stack pointer
    mov rsp, rsi      ; switch onto the incoming task's stack

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret               ; jumps to whatever return address sits on the new stack --
                       ; either back into yield()'s caller (resuming a task), or
                       ; into task_entry_trampoline (starting a brand new one)

section .note.GNU-stack noalloc noexec nowrite progbits
