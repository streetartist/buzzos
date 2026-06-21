%define KERNEL_LOAD_ADDR 0x1000
%define KERNEL_SECTORS 64

bits 16
org 0x7c00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl
    mov si, loading_msg
    call print

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc disk_error

    in al, 0x92
    or al, 00000010b
    out 0x92, al

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:protected_start

disk_error:
    mov si, disk_error_msg
    call print

.halt:
    hlt
    jmp .halt

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

boot_drive db 0
loading_msg db "BuzzOS boot", 13, 10, 0
disk_error_msg db "Disk error", 13, 10, 0

align 4
dap:
    db 0x10
    db 0
    dw KERNEL_SECTORS
    dw KERNEL_LOAD_ADDR
    dw 0x0000
    dq 1

align 8
gdt_start:
    dq 0

gdt_code:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

bits 32
protected_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_LOAD_ADDR

times 510 - ($ - $$) db 0
dw 0xaa55

