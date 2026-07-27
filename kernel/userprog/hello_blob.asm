section .rodata

global user_hello_start
global user_hello_end

user_hello_start:
incbin "kernel/userprog/hello.bin"
user_hello_end:

section .note.GNU-stack noalloc noexec nowrite progbits
