%define KERNEL_FINAL_ADDR 0x1000
%define KERNEL_HIGH_SEG   0x1000   ; ES segment → physical 0x10000
%define KERNEL_SECTORS    256      ; up to 128 KiB, loaded at 0x10000
%define READ_CHUNK        64

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

    mov [drive], dl
    mov si, loading_msg
    call print

    ; Query E820 memory map. Buffer at 0x500, count at 0x4F8.
    mov di, 0x500
    xor ebx, ebx
    mov edx, 0x534D4150
    mov word [0x4F8], 0
.e820_loop:
    mov eax, 0x0000E820
    mov ecx, 24
    int 0x15
    jc  .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    cmp cl, 20
    jb  .e820_done
    inc word [0x4F8]
    add di, 24
    cmp ebx, 0
    jne .e820_loop
.e820_done:

    ; Load kernel via LBA extended reads to physical 0x10000 (above bootloader).
    ; Reading in chunks avoids BIOS single-request limits.
    mov word [sectors_left], KERNEL_SECTORS
    mov word [load_seg], KERNEL_HIGH_SEG
    mov word [dap_lba], 1
.load_loop:
    mov ax, [sectors_left]
    test ax, ax
    jz .load_done
    cmp ax, READ_CHUNK
    jbe .load_count_ok
    mov ax, READ_CHUNK
.load_count_ok:
    mov [dap_count], ax
    mov ax, [load_seg]
    mov [dap_seg], ax
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13
    jc .disk_error
    mov ax, [dap_count]
    sub [sectors_left], ax
    add [dap_lba], ax
    shl ax, 5                  ; sectors * 512 / 16 = paragraphs
    add [load_seg], ax
    jmp .load_loop
.load_done:

    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:protected_start

.disk_error:
    mov si, err_msg
    call print
    hlt

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

drive       db 0
sectors_left dw 0
load_seg    dw 0
loading_msg db "BuzzOS boot", 13, 10, 0
err_msg     db "Disk error", 13, 10, 0

; Disk Address Packet for int 13h/42h (LBA extended read)
align 4
dap:
    db 0x10                  ; packet size (16 bytes)
    db 0                     ; reserved
dap_count:
    dw 0                     ; number of sectors to read
    dw 0x0000                ; dest offset  (0x1000:0x0000 = phys 0x10000)
dap_seg:
    dw KERNEL_HIGH_SEG       ; dest segment
dap_lba:
    dq 1                     ; starting LBA (boot sector = LBA 0, kernel = LBA 1)

; GDT: 6 entries (NULL, kcode32, kdata32, ucode32, udata32, TSS)
gdt_start:
    ; 0x00: NULL
    dq 0
    ; 0x08: kcode32 — 0..4GiB, DPL=0, exec/read
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
    ; 0x10: kdata32 — 0..4GiB, DPL=0, read/write
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
    ; 0x18: ucode32 — 0..4GiB, DPL=3, exec/read
    dw 0xffff
    dw 0x0000
    db 0x00
    db 11111010b
    db 11001111b
    db 0x00
    ; 0x20: udata32 — 0..4GiB, DPL=3, read/write
    dw 0xffff
    dw 0x0000
    db 0x00
    db 11110010b
    db 11001111b
    db 0x00
    ; 0x28: TSS placeholder — kernel fills base at runtime
    dw 0x0067        ; limit = 103
    dw 0x0000        ; base low (patched)
    db 0x00          ; base mid
    db 10001001b     ; P=1, DPL=0, 32-bit TSS available
    db 00000000b     ; granularity
    db 0x00          ; base high
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG  equ 0x08
DATA_SEG  equ 0x10

bits 32
protected_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; The rep movsd below copies data OVER the boot sector at 0x7C00 (where
    ; this code lives), so we first relocate a position-independent copy
    ; routine to 0x600 and jump there.
    mov esi, .copy_stub
    mov edi, 0x600
    mov ecx, (.copy_stub_end - .copy_stub + 3) / 4
    cld
    rep movsd
    mov eax, 0x600
    jmp eax

; Position-independent stub — uses only absolute moves and indirect jump.
.copy_stub:
    mov esi, 0x10000
    mov edi, 0x1000
    mov ecx, (KERNEL_SECTORS * 512) / 4
    cld
    rep movsd
    mov eax, 0x1000
    jmp eax
.copy_stub_end:

times 510 - ($ - $$) db 0
dw 0xaa55
