OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5); /* R + X -- FILEHDR/PHDRS here is what actually maps the ELF header and program-header table into memory at all. Without them, file offset 0 (where those live) just isn't part of any LOAD segment -- a perfectly valid ELF file, just one where AT_PHDR has nowhere real to point, since the loader never reads that part of the file into the process in the first place. */
    data PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame)
        *(.note.*)
        *(.comment)
    }
}
