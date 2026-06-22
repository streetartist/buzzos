#include <stddef.h>
#include <stdint.h>
#include "exec.h"
#include "reboot.h"
#include "serial.h"
#include "syscall_internal.h"
#include "task.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"

static volatile int exec_syscall_lock;

static int spawn_proc_common(const char *path, int flags, int argc, const char *const argv[]) {
    static uint8_t elf_buf[65536];

    while (__sync_lock_test_and_set(&exec_syscall_lock, 1))
        task_yield();

    int fd = vfs_open_flags(path, O_RDONLY);
    if (fd < 0) {
        __sync_lock_release(&exec_syscall_lock);
        return -1;
    }

    int total = 0;
    int n;
    while ((n = vfs_read(fd, elf_buf + total, sizeof(elf_buf) - (size_t)total)) > 0)
        total += n;
    vfs_close(fd);
    if (n < 0 || total < 52 || total == (int)sizeof(elf_buf)) {
        __sync_lock_release(&exec_syscall_lock);
        return -1;
    }

    const char *name = path;
    for (int i = 0; path && path[i]; i++)
        if (path[i] == '/')
            name = path + i + 1;
    int silent = (flags & 1u) ? 1 : 0;
    int pid = exec_start_args(elf_buf, (size_t)total, name, silent, argc, argv);
    __sync_lock_release(&exec_syscall_lock);
    return pid;
}

int sys_exit(uint32_t code, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!current_task->console_silent) {
        serial_puts("[syscall] exit(");
        serial_puthex((uint32_t)code);
        serial_puts(") task=");
        serial_puthex((uint32_t)current_task->id);
        serial_puts("\n");
    }

    task_exit_code((int)code);
    for (;;) { __asm__ volatile("hlt"); }
    return 0;
}

int sys_spawn_proc(uint32_t path_arg, uint32_t flags, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    const char *path = (const char *)(uintptr_t)path_arg;
    if (!user_string_ok(path))
        return -1;
    const char *argv[1] = { path };
    return spawn_proc_common(path, (int)flags, 1, argv);
}

int sys_spawn_proc_args(uint32_t path_arg, uint32_t argv_arg, uint32_t argc_arg,
                        uint32_t flags, uint32_t e) {
    (void)e;
    const char *path = (const char *)(uintptr_t)path_arg;
    const char *const *user_argv = (const char *const *)(uintptr_t)argv_arg;
    int argc = (int)argc_arg;
    if (!user_string_ok(path))
        return -1;
    if (argc < 0)
        return -1;
    if (argc > 15)
        argc = 15;
    if (argc > 0 && !user_range_ok(argv_arg, (uint32_t)argc * sizeof(char *)))
        return -1;

    const char *argv[16];
    for (int i = 0; i < argc; i++) {
        if (user_argv && !user_string_ok(user_argv[i]))
            return -1;
        argv[i] = user_argv ? user_argv[i] : "";
    }
    if (argc == 0) {
        argv[0] = path;
        argc = 1;
    }
    return spawn_proc_common(path, (int)flags, argc, argv);
}

int sys_ps(uint32_t buf, uint32_t size, uint32_t show_dead, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!user_range_ok(buf, size))
        return -1;
    return task_dump_text((char *)(uintptr_t)buf, (int)size, (int)show_dead);
}

int sys_reboot(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    machine_reboot();
}

struct thread_info {
    uint32_t func_addr;
    uint32_t stack_top;
};

static struct thread_info thread_infos[MAX_TASKS];
static int process_thread_count[MAX_TASKS];

void syscall_reset_process(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS)
        return;
    process_thread_count[task_id] = 0;
}

static void thread_trampoline(void) {
    int id = current_task->id;
    uint32_t func = thread_infos[id].func_addr;
    uint32_t stack = thread_infos[id].stack_top;
    user_enter(func, stack);
}

int sys_spawn(uint32_t func_addr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    uint32_t return_addr = b;
    if (!user_range_ok(func_addr, 1) || !user_range_ok(return_addr, 1))
        return -1;
    int owner = current_task ? current_task->fd_owner : 0;
    if (owner < 0 || owner >= MAX_TASKS)
        return -1;

    int slot = ++process_thread_count[owner];
    uint32_t user_stack = USER_DEFAULT_STACK_TOP - (uint32_t)(slot * 0x4000);
    if (user_stack < 0x001C0004)
        return -1;
    user_stack -= 4;
    *(uint32_t *)(uintptr_t)user_stack = return_addr;

    __asm__ volatile("cli");
    int id = task_create(thread_trampoline, "user_thread");
    if (id < 0) {
        __asm__ volatile("sti");
        return -1;
    }

    thread_infos[id].func_addr = func_addr;
    thread_infos[id].stack_top = user_stack;
    task_set_fd_owner(id, owner);
    __asm__ volatile("sti");
    return id;
}

int sys_yield(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    task_yield();
    return 0;
}

int sys_join(uint32_t tid_arg, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    int tid = (int)tid_arg;
    for (;;) {
        int state = task_get_state(tid);
        if (state < 0)
            return -1;
        if (state == TASK_DEAD)
            break;
        task_yield();
    }
    task_forget_dead(tid);
    return 0;
}

int sys_sleep(uint32_t ms, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    timer_sleep_ms(ms);
    return 0;
}

int sys_kill(uint32_t pid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return task_kill((int)pid);
}

int sys_getpid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return task_get_pid();
}

int sys_gettid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return task_get_tid();
}

int sys_chdir(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path))
        return -1;
    return vfs_chdir((const char *)(uintptr_t)path);
}

int sys_getcwd(uint32_t buf, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!user_range_ok(buf, size))
        return -1;
    return vfs_getcwd((char *)(uintptr_t)buf, (size_t)size);
}

int sys_waitpid(uint32_t pid, uint32_t status, uint32_t options, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (status && !user_range_ok(status, sizeof(int)))
        return -1;
    return task_wait_pid((int)pid, (int *)(uintptr_t)status, (int)options);
}
