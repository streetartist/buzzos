#include "syscall.h"
#include "serial.h"
#include "vfs.h"

static int sys_exit(int code) {
    serial_puts("[syscall] exit(");
    serial_puthex((uint32_t)code);
    serial_puts(")\n");
    for (;;) { __asm__ volatile("hlt"); }
    return 0;
}

static int sys_write(int fd, const void *buf, size_t count) {
    return vfs_write(fd, buf, count);
}

static int sys_read(int fd, void *buf, size_t count) {
    return vfs_read(fd, buf, count);
}

static int sys_open(const char *path) {
    return vfs_open(path);
}

static int sys_close(int fd) {
    return vfs_close(fd);
}

typedef int (*syscall_handler_fn)(int, ...);
static syscall_handler_fn syscall_table[256];

void syscall_handler(struct syscall_frame *frame) {
    uint32_t eax = frame->eax;
    uint32_t ebx = frame->ebx;
    uint32_t ecx = frame->ecx;
    uint32_t edx = frame->edx;
    uint32_t esi = frame->esi;
    uint32_t edi = frame->edi;
    (void)esi; (void)edi;

    int nr = (int)eax;
    int result = -1;
    if (nr < 256 && syscall_table[nr])
        result = syscall_table[nr](ebx, ecx, edx, esi, edi);

    frame->eax = (uint32_t)result;
}

void syscall_init(void) {
    syscall_table[SYS_EXIT]  = (syscall_handler_fn)sys_exit;
    syscall_table[SYS_OPEN]  = (syscall_handler_fn)sys_open;
    syscall_table[SYS_CLOSE] = (syscall_handler_fn)sys_close;
    syscall_table[SYS_READ]  = (syscall_handler_fn)sys_read;
    syscall_table[SYS_WRITE] = (syscall_handler_fn)sys_write;
    serial_puts("[syscall] VFS-backed syscalls ready\n");
}
