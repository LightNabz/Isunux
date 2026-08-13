bits 64

global _start
global environ
extern main

section .bss
environ: resq 1   ; char **environ -- storage lives here, mini_libc.h's getenv() reads it (extern declaration there)

section .text
_start:
    ; per the layout build_initial_stack() wrote:
    ;   [rsp]    = argc
    ;   [rsp+8]  = argv[0]  (start of the argv array)
    mov rdi, [rsp]              ; argc
    lea rsi, [rsp + 8]           ; argv

    ; envp starts right after argv's NULL terminator: argv has (argc+1)
    ; slots (argc pointers + one NULL), so envp = argv + (argc+1)*8
    mov rax, rdi
    lea rdx, [rsi + rax*8 + 8]    ; envp
    mov [environ], rdx            ; stash it globally too, for getenv() -- not every program bothers declaring envp as main's 3rd parameter

    call main                     ; a real `call`, so main's prologue sees
                                   ; exactly the stack state any normal
                                   ; function call would produce

    mov edi, eax                  ; exit code = main's return value
                                   ; (writing edi zero-extends into rdi)
    mov rax, 1                    ; SYS_EXIT
    syscall                       ; clobbers rcx/r11 (return addr/flags) --
                                   ; irrelevant here, we never return

.hang:                            ; unreachable -- sys_exit never returns
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
