section .rodata

global user_shell_elf_start
global user_shell_elf_end

user_shell_elf_start:
incbin "kernel/userprog/shell_elf"
user_shell_elf_end:

section .note.GNU-stack noalloc noexec nowrite progbits
