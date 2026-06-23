#include <stddef.h>
#include <stdint.h>
#include "exec.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "net.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "syscall.h"
#include "task.h"
#include "timer.h"
#include "vfs.h"
#include "vga.h"
#include "initrd.h"

extern uint8_t __bss_start, __bss_end;

static void zero_bss(void) {
    for (uint8_t *p = &__bss_start; p < &__bss_end; p++) *p = 0;
}

__attribute__((section(".text.entry"), used, noreturn))
void _start(void) {
    zero_bss();
    serial_init();
    serial_puts("[boot] BuzzOS starting\n");
    gdt_install();
    idt_install();

    /* Enable x87 FPU: clear CR0.EM (bit 2), set CR0.MP (bit 1),
     * then finit to reset the FPU state. */
    {
        uint32_t cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 = (cr0 & ~(1u << 2)) | (1u << 1);  /* clear EM, set MP */
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
        __asm__ volatile("finit");
        serial_puts("[boot] x87 FPU enabled\n");
    }
    syscall_init();
    pmm_init();
    paging_init();
    keyboard_init();
    vfs_init();
    vfs_mkdir("/bin");
    ramfs_register("/hello", initrd_hello_data, INITRD_HELLO_SIZE);
    ramfs_register("/bin/sh", initrd_bin_sh_data, INITRD_BIN_SH_SIZE);
    ramfs_register("/bin/nano", initrd_bin_nano_data, INITRD_BIN_NANO_SIZE);
    ramfs_register("/bin/basm", initrd_bin_basm_data, INITRD_BIN_BASM_SIZE);
    net_init();
    vga_init();
    vga_set_color(0x0F, 0x00);

    sched_init();
    timer_init();
    if (exec_start(initrd_bin_sh_data, INITRD_BIN_SH_SIZE, "sh", 0) < 0)
        serial_puts("[boot] failed to start user shell\n");
    else
        serial_puts("[boot] user shell started\n");
    for (;;) { task_yield(); }
}
