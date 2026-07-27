bits 64
org 0x400000

; ISUNUX's ring-3 test program, now exercising the real VFS: open a
; tmpfs file, read it, and write what came back out to stdout -- proving
; a full open->read->write->close round trip through real syscalls, not
; just a hardcoded write to serial like milestone 6's version of this
; file did.

%define SYS_WRITE 0
%define SYS_EXIT  1
%define SYS_OPEN  2
%define SYS_READ  3
%define SYS_CLOSE 4

_start:
    ; write(1, msg1, msg1_len) -- fd 1 is stdout, routed through the
    ; console vnode, same process_write() path as any real file
    mov rdi, 1
    mov rsi, msg1
    mov rdx, msg1_len
    mov rax, SYS_WRITE
    int 0x80

    ; fd = open("/hello.txt")
    mov rdi, path
    mov rax, SYS_OPEN
    int 0x80
    mov [fd_val], rax

    ; n = read(fd, buf, buf_cap)
    mov rdi, [fd_val]
    mov rsi, buf
    mov rdx, buf_cap
    mov rax, SYS_READ
    int 0x80
    mov [bytes_read], rax

    ; write(1, buf, n) -- print exactly what came back from tmpfs
    mov rdi, 1
    mov rsi, buf
    mov rdx, [bytes_read]
    mov rax, SYS_WRITE
    int 0x80

    ; close(fd)
    mov rdi, [fd_val]
    mov rax, SYS_CLOSE
    int 0x80

    ; exit(0)
    mov rdi, 0
    mov rax, SYS_EXIT
    int 0x80

.hang:                ; should never get here -- sys_exit never returns
    jmp .hang

msg1: db "starting vfs test: opening /hello.txt", 10
msg1_len equ $ - msg1

path: db "/hello.txt", 0

buf_cap equ 128
buf: times buf_cap db 0

fd_val: dq 0
bytes_read: dq 0
