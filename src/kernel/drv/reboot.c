#include <stdint.h>
#include "io.h"
#include "reboot.h"

static int kbc_input_ready(void) {
    return (inb(0x64) & 0x02) == 0;
}

static void short_delay(void) {
    for (int i = 0; i < 10000; i++)
        io_wait();
}

static void try_kbc_reboot(void) {
    for (int i = 0; i < 100000; i++) {
        if (kbc_input_ready())
            break;
        io_wait();
    }
    outb(0x64, 0xFE);
    short_delay();
}

static void try_cf9_reboot(void) {
    outb(0xCF9, 0x02);
    io_wait();
    outb(0xCF9, 0x06);
    short_delay();
}

void machine_reboot(void) {
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };

    __asm__ volatile("cli" ::: "memory");
    try_kbc_reboot();
    try_cf9_reboot();

    __asm__ volatile(
        "lidt %0\n"
        "int $3\n"
        :
        : "m"(null_idt)
        : "memory");

    for (;;)
        __asm__ volatile("hlt");
}
