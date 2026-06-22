#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "net.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "syscall.h"
#include "task.h"
#include "vfs.h"
#include "vga.h"

extern uint8_t __bss_start, __bss_end;
extern void shell_task(void);

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
    syscall_init();
    pmm_init();
    paging_init();
    keyboard_init();
    vfs_init();
    net_init();
    vga_init();
    vga_set_color(0x0F, 0x00);

    sched_init();
    task_create(shell_task, "shell");
    serial_puts("[boot] shell started\n");
    for (;;) { task_yield(); }
}
