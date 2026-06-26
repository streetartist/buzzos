bits 32

global _start
extern kernel_main

section .multiboot
align 8
mb2_header_start:
    dd 0xE85250D6
    dd 0
    dd mb2_header_end - mb2_header_start
    dd -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))

    ; Request a linear framebuffer, but keep the kernel side free to
    ; switch to another backend if needed after boot.
    dw 5
    dw 0
    dd 20
    dd 1280
    dd 800
    dd 32

align 8
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .text.entry
_start:
    cli
    mov esp, 0x700000
    push ebx
    push eax
    call kernel_main
.hang:
    hlt
    jmp .hang
