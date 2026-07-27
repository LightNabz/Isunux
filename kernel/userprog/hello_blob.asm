section .rodata

global user_hello_elf_start
global user_hello_elf_end

user_hello_elf_start:
incbin "kernel/userprog/hello_elf"
user_hello_elf_end:

section .note.GNU-stack noalloc noexec nowrite progbits
