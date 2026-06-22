#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "serial.h"
#include "user.h"

enum {
    USER_TRAMPOLINE_BASE = 0x1FF000,
    USER_MODE_EFLAGS = 0x0202,
    USER_ENTRY_PATCH_OFFSET = 13,
};

#if defined(__INTELLISENSE__)
#define GNU_ASM(...)
#else
#define GNU_ASM(...) __asm__ volatile(__VA_ARGS__)
#endif

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
    for (size_t i = 0; i < len; i++)
        dst[i] = src[i];
}

static void patch_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t install_user_trampoline(uint32_t entry) {
    static const uint8_t trampoline_code[] = {
        0x66, 0xB8, 0x23, 0x00, /* mov ax, 0x23 */
        0x8E, 0xD8,             /* mov ds, ax */
        0x8E, 0xC0,             /* mov es, ax */
        0x8E, 0xE0,             /* mov fs, ax */
        0x8E, 0xE8,             /* mov gs, ax */
        0xB8, 0x00, 0x00, 0x00, 0x00, /* mov eax, entry */
        0xFF, 0xE0,             /* jmp eax */
    };
    uint8_t *trampoline = (uint8_t *)USER_TRAMPOLINE_BASE;

    copy_bytes(trampoline, trampoline_code, sizeof(trampoline_code));
    patch_u32(trampoline + USER_ENTRY_PATCH_OFFSET, entry);

    return USER_TRAMPOLINE_BASE;
}

void user_enter(uint32_t entry, uint32_t stack_top) {
    uint32_t trampoline_entry = install_user_trampoline(entry);

    serial_puts("[user] enter entry=");
    serial_puthex(entry);
    serial_puts(" stack=");
    serial_puthex(stack_top);
    serial_puts("\n");

    GNU_ASM(
        "pushl %0\n"
        "pushl %1\n"
        "pushl %2\n"
        "pushl %3\n"
        "pushl %4\n"
        "iretl\n"
        :
        : "i"(GDT_SEL_UDATA32),
          "r"(stack_top),
          "i"(USER_MODE_EFLAGS),
          "i"(GDT_SEL_UCODE32),
          "r"(trampoline_entry)
        : "esp"
    );

    __builtin_unreachable();
}