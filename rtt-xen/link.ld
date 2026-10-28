ENTRY(_image_start)
SECTIONS
{
    . = 0x40080000;
    _image_start = .;
    .text : { KEEP(*(.text.boot)) KEEP(*(.text.vectors)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    _bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    _bss_end = .;
    . += 16384;
    _boot_stack_top = .;
    . = ALIGN(4096);
    _image_end = .;
    /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
