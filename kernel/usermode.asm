bits 64

; void enter_userspace(uint64_t user_rip, uint64_t user_rsp)
; rdi = the user program's entry point
; rsi = the top of the user stack to hand it
;
; Never returns -- this is a one-way trip into ring 3. (Getting back
; into ring 0 afterward happens via syscall_entry's own sysretq on the
; way OUT of a syscall, not by this function ever regaining control.)
global enter_userspace
enter_userspace:
    mov ax, 0x23         ; GDT_USER_DATA (0x20 | RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; SS is deliberately not loaded here -- iretq sets it from the frame
    ; we're about to build below, which is the only correct way to load
    ; SS with a DPL=3 selector while still executing at CPL=0.

    push 0x23            ; SS  = user data selector
    push rsi              ; RSP = user stack pointer

    pushfq                ; grab current RFLAGS as a starting point...
    pop rax
    or rax, 0x200         ; ...and force IF=1, so interrupts (the timer,
                           ; the scheduler) still work while ring 3 runs
    push rax               ; RFLAGS

    push 0x2b               ; CS  = GDT_USER_CODE (0x28 | RPL 3)
    push rdi                ; RIP = user entry point

    iretq                    ; pops RIP,CS,RFLAGS,RSP,SS and jumps to ring 3

section .note.GNU-stack noalloc noexec nowrite progbits
