; === Context switch =======================================================
; void switch_context(uint32_t **old_esp_ptr, uint32_t *new_esp);
;
; Saves the current register state onto the stack, stores ESP into
; *old_esp_ptr, loads ESP from new_esp, restores state from the new
; stack, and returns to wherever the new task left off.
; ===========================================================================

bits 32
section .text
global switch_context

switch_context:
    ; Save caller-saved + callee-saved registers + eflags
    pushfd
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    ; old_esp_ptr is at [esp + 36] (8 pushes × 4 + return addr)
    mov eax, [esp + 36]
    mov [eax], esp          ; *old_esp_ptr = current ESP

    ; new_esp is at [esp + 40]
    mov esp, [esp + 40]     ; switch stacks

    ; Restore state from new stack
    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax
    popfd
    ret
