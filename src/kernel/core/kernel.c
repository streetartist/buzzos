#include <stddef.h>
#include <stdint.h>
#include "exec.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "mouse.h"
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
#include "app_registry.h"

extern uint8_t __bss_start, __bss_end;

static void zero_bss(void) {
    for (uint8_t *p = &__bss_start; p < &__bss_end; p++) *p = 0;
}

static void seed_file_if_missing(const char *path, const void *data, size_t size) {
    struct stat st;
    if (vfs_stat(path, &st) == 0)
        return;
    if (vfs_write_file(path, data, size) < 0) {
        serial_puts("[boot] failed to seed ");
        serial_puts(path);
        serial_puts("\n");
    }
}

static void seed_file_if_missing_or_size_changed(const char *path,
                                                 const void *data,
                                                 size_t size) {
    struct stat st;
    if (vfs_stat(path, &st) == 0 && st.st_size == size)
        return;
    if (vfs_write_file(path, data, size) < 0) {
        serial_puts("[boot] failed to refresh ");
        serial_puts(path);
        serial_puts("\n");
    }
}

static void seed_user_apps(void) {
    (void)vfs_mkdir("/fs/apps");
    for (size_t i = 0; i < sizeof(app_seed_entries) / sizeof(app_seed_entries[0]); i++) {
        const struct app_seed_entry *entry = &app_seed_entries[i];
        if (entry->refresh)
            seed_file_if_missing_or_size_changed(entry->path, entry->data, entry->size);
        else
            seed_file_if_missing(entry->path, entry->data, entry->size);
    }
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
    mouse_init();
    serial_puts("[boot] input devices ok\n");
    vfs_init();
    serial_puts("[boot] vfs init ok\n");
    vfs_mkdir("/bin");
    ramfs_register("/hello", initrd_hello_data, INITRD_HELLO_SIZE);
    ramfs_register("/bin/sh", initrd_bin_sh_data, INITRD_BIN_SH_SIZE);
    ramfs_register("/bin/nano", initrd_bin_nano_data, INITRD_BIN_NANO_SIZE);
    ramfs_register("/bin/basm", initrd_bin_basm_data, INITRD_BIN_BASM_SIZE);
    ramfs_register("/bin/gui", initrd_bin_gui_data, INITRD_BIN_GUI_SIZE);
    ramfs_register("/bin/futexhold", initrd_bin_futexhold_data, INITRD_BIN_FUTEXHOLD_SIZE);
    ramfs_register("/bin/cat", initrd_bin_cat_data, INITRD_BIN_CAT_SIZE);
    ramfs_register("/bin/echo", initrd_bin_echo_data, INITRD_BIN_ECHO_SIZE);
    serial_puts("[boot] initrd files registered\n");
    seed_user_apps();
    serial_puts("[boot] user apps seeded\n");
    net_init();
    serial_puts("[boot] net init ok\n");
    vga_init();
    serial_puts("[boot] vga init ok\n");
    vga_set_color(0x0F, 0x00);

    sched_init();
    timer_init();
    if (exec_start(initrd_bin_sh_data, INITRD_BIN_SH_SIZE, "sh", 0) < 0)
        serial_puts("[boot] failed to start user shell\n");
    else
        serial_puts("[boot] user shell started\n");
    for (;;) { task_yield(); }
}
