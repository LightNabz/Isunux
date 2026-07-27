bits 64
org 0x400000

; ISUNUX's first ring-3 program. No libc, no ELF -- just two syscalls,
; assembled straight to a flat binary and mapped where the kernel tells
; it to live. `org 0x400000` makes every label below resolve to the
; real virtual address it'll have once mapped, so no relocation needed.

_start:
    mov rdi, msg      ; arg 1: buffer
    mov rsi, msg_len  ; arg 2: length
    mov rax, 0        ; SYS_WRITE
    int 0x80

    mov rdi, 0        ; exit code 0
    mov rax, 1        ; SYS_EXIT
    int 0x80

.hang:                ; should never get here -- sys_exit never returns
    jmp .hang

msg: db "hello from ring 3! isunux says hi.", 10
msg_len equ $ - msg
