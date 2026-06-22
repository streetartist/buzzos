#include <stdint.h>
#include "syscall_internal.h"
#include "task.h"
#include "vfs.h"

enum {
    MAX_FUTEX_WAITERS = 32,
};

struct futex_waiter {
    int used;
    int task_id;
    uint32_t addr;
    int woken;
};

static struct futex_waiter futex_waiters[MAX_FUTEX_WAITERS];
static volatile int futex_locked;

static void futex_lock(void) {
    while (__sync_lock_test_and_set(&futex_locked, 1))
        task_yield();
}

static void futex_unlock(void) {
    __sync_lock_release(&futex_locked);
}

int sys_pipe(uint32_t fds_arg, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_range_ok(fds_arg, sizeof(int) * 2))
        return -1;
    return vfs_pipe((int *)(uintptr_t)fds_arg);
}

int sys_futex_wait(uint32_t addr_arg, uint32_t expected, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!user_range_ok(addr_arg, sizeof(int)))
        return -1;
    volatile int *addr = (volatile int *)(uintptr_t)addr_arg;
    if (*addr != (int)expected)
        return 0;

    int slot = -1;
    futex_lock();
    for (int i = 0; i < MAX_FUTEX_WAITERS; i++) {
        if (!futex_waiters[i].used) {
            slot = i;
            futex_waiters[i].used = 1;
            futex_waiters[i].task_id = current_task ? current_task->id : 0;
            futex_waiters[i].addr = addr_arg;
            futex_waiters[i].woken = 0;
            break;
        }
    }
    futex_unlock();
    if (slot < 0)
        return -1;

    for (;;) {
        int woken;
        futex_lock();
        woken = futex_waiters[slot].woken;
        futex_unlock();
        if (woken || *addr != (int)expected)
            break;
        task_yield();
    }

    futex_lock();
    futex_waiters[slot].used = 0;
    futex_unlock();
    return 0;
}

int sys_futex_wake(uint32_t addr_arg, uint32_t count, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!user_range_ok(addr_arg, sizeof(int)))
        return -1;
    int woke = 0;
    futex_lock();
    for (int i = 0; i < MAX_FUTEX_WAITERS; i++) {
        if (futex_waiters[i].used && futex_waiters[i].addr == addr_arg && !futex_waiters[i].woken) {
            futex_waiters[i].woken = 1;
            woke++;
            if (count && woke >= (int)count)
                break;
        }
    }
    futex_unlock();
    return woke;
}
