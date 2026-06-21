ENTRY(_start)

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text.entry)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss*)
    }
    __bss_end = .;
}
