; Minimal setjmp/longjmp for kernel use.
; Used by exec to save shell context and let sys_exit restore it.
;
; int kernel_setjmp(jmp_buf env);    — returns 0 on save, non-zero on longjmp
; void kernel_longjmp(jmp_buf env, int val);  — restores, makes setjmp return val
;
; jmp_buf layout (6 uint32_t):  [ebx, esi, edi, ebp, esp, eip]

bits 32
section .text

global kernel_setjmp
global kernel_longjmp

; int kernel_setjmp(uint32_t env[6])
kernel_setjmp:
    mov eax, [esp + 4]      ; eax = env pointer
    mov [eax + 0],  ebx
    mov [eax + 4],  esi
    mov [eax + 8],  edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]      ; caller's ESP (after popping return addr)
    mov [eax + 16], ecx
    mov ecx, [esp]           ; return address
    mov [eax + 20], ecx
    xor eax, eax             ; return 0
    ret

; void kernel_longjmp(uint32_t env[6], int val)
kernel_longjmp:
    mov edx, [esp + 4]      ; edx = env pointer
    mov eax, [esp + 8]      ; eax = val (return value for setjmp)
    test eax, eax
    jnz .nonzero
    inc eax                  ; ensure non-zero
.nonzero:
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]          ; jump to saved return address
