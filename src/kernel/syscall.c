#include "syscall.h"
#include "exec.h"
#include "net.h"
#include "reboot.h"
#include "serial.h"
#include "task.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"

typedef int (*syscall_handler_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

enum {
    USER_PTR_START = 0x001C0000u,
    USER_PTR_END   = 0x00230000u,
    AF_INET_K       = 2,
    SOCK_STREAM_K   = 1,
    SOCK_DGRAM_K    = 2,
    SOCK_RAW_K      = 3,
    IPPROTO_ICMP_K  = 1,
    IPPROTO_UDP_K   = 17,
    MAX_SOCKETS     = 8,
};

struct k_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
};

struct socket_entry {
    int used;
    int owner;
    int domain;
    int type;
    int protocol;
    uint16_t local_port;
    uint32_t peer_ip;
    uint16_t peer_port;
    int connected;
};

static struct socket_entry sockets[MAX_SOCKETS];
static volatile int socket_locked;
static int active_tcp_socket = -1;

static uint16_t ntoh16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static void socket_lock(void) {
    while (__sync_lock_test_and_set(&socket_locked, 1))
        task_yield();
}

static void socket_unlock(void) {
    __sync_lock_release(&socket_locked);
}

static int socket_owner(void) {
    if (!current_task)
        return 0;
    if (current_task->fd_owner >= 0 && current_task->fd_owner < MAX_TASKS)
        return current_task->fd_owner;
    return current_task->id;
}

static struct socket_entry *socket_get(int sd) {
    if (sd < 0 || sd >= MAX_SOCKETS)
        return 0;
    if (!sockets[sd].used || sockets[sd].owner != socket_owner())
        return 0;
    return &sockets[sd];
}

static int user_range_ok(uint32_t ptr, uint32_t len) {
    if (len == 0)
        return 1;
    if (ptr < USER_PTR_START || ptr >= USER_PTR_END)
        return 0;
    if (len > USER_PTR_END - ptr)
        return 0;
    return 1;
}

static int user_string_ok(const char *s) {
    uint32_t ptr = (uint32_t)(uintptr_t)s;
    if (!user_range_ok(ptr, 1))
        return 0;
    for (uint32_t i = 0; i < 256; i++) {
        if (!user_range_ok(ptr + i, 1))
            return 0;
        if (s[i] == 0)
            return 1;
    }
    return 0;
}

static int sys_exit(uint32_t code, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!current_task->console_silent) {
        serial_puts("[syscall] exit(");
        serial_puthex((uint32_t)code);
        serial_puts(") task=");
        serial_puthex((uint32_t)current_task->id);
        serial_puts("\n");
    }

    task_exit_code((int)code);
    /* not reached */
    for (;;) { __asm__ volatile("hlt"); }
    return 0;
}

static int sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!user_range_ok(buf, count))
        return -1;
    return vfs_write((int)fd, (const void *)(uintptr_t)buf, (size_t)count);
}

static int sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!user_range_ok(buf, count))
        return -1;
    return vfs_read((int)fd, (void *)(uintptr_t)buf, (size_t)count);
}

static int sys_close(uint32_t fd, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_close((int)fd);
}

static int sys_dup(uint32_t fd, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_dup((int)fd);
}

static int sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return vfs_dup2((int)oldfd, (int)newfd);
}

static int sys_stat(uint32_t path, uint32_t st, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path) || !user_range_ok(st, sizeof(struct stat)))
        return -1;
    return vfs_stat((const char *)(uintptr_t)path, (struct stat *)(uintptr_t)st);
}

static int sys_getdents(uint32_t fd, uint32_t ents, uint32_t count, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!user_range_ok(ents, count))
        return -1;
    return vfs_getdents((int)fd, (struct dirent *)(uintptr_t)ents, (size_t)count);
}

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

static int sys_spawn_proc(uint32_t path_arg, uint32_t flags, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    const char *path = (const char *)(uintptr_t)path_arg;
    if (!user_string_ok(path))
        return -1;
    const char *argv[1] = { path };
    return spawn_proc_common(path, (int)flags, 1, argv);
}

static int sys_spawn_proc_args(uint32_t path_arg, uint32_t argv_arg, uint32_t argc_arg,
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

static int sys_ps(uint32_t buf, uint32_t size, uint32_t show_dead, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!user_range_ok(buf, size))
        return -1;
    return task_dump_text((char *)(uintptr_t)buf, (int)size, (int)show_dead);
}

static int sys_reboot(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    machine_reboot();
}

static int sys_mkdir(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path))
        return -1;
    return vfs_mkdir((const char *)(uintptr_t)path);
}

static int sys_unlink(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path))
        return -1;
    return vfs_remove((const char *)(uintptr_t)path);
}

static int sys_create(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path))
        return -1;
    return vfs_create((const char *)(uintptr_t)path);
}

static int sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return vfs_lseek((int)fd, (int)offset, (int)whence);
}

static int sys_rmdir(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path))
        return -1;
    return vfs_rmdir((const char *)(uintptr_t)path);
}

static int sys_rename(uint32_t old_path, uint32_t new_path, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)old_path) ||
        !user_string_ok((const char *)(uintptr_t)new_path))
        return -1;
    return vfs_rename((const char *)(uintptr_t)old_path, (const char *)(uintptr_t)new_path);
}

static int sys_socket(uint32_t domain, uint32_t type, uint32_t protocol, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if ((int)domain != AF_INET_K)
        return -1;
    if (!((int)type == SOCK_STREAM_K && protocol == 0) &&
        !((int)type == SOCK_DGRAM_K && (protocol == 0 || protocol == IPPROTO_UDP_K)) &&
        !((int)type == SOCK_RAW_K && protocol == IPPROTO_ICMP_K))
        return -1;
    socket_lock();
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            sockets[i].used = 1;
            sockets[i].owner = socket_owner();
            sockets[i].domain = (int)domain;
            sockets[i].type = (int)type;
            sockets[i].protocol = (int)protocol;
            sockets[i].local_port = (uint16_t)(49152 + i);
            sockets[i].peer_ip = 0;
            sockets[i].peer_port = 0;
            sockets[i].connected = 0;
            socket_unlock();
            return i;
        }
    }
    socket_unlock();
    return -1;
}

static int sys_connect(uint32_t sd_arg, uint32_t addr_arg, uint32_t addrlen,
                       uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (addrlen < sizeof(struct k_sockaddr_in) ||
        !user_range_ok(addr_arg, sizeof(struct k_sockaddr_in)))
        return -1;
    struct k_sockaddr_in *addr = (struct k_sockaddr_in *)(uintptr_t)addr_arg;
    if (addr->sin_family != AF_INET_K)
        return -1;

    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s) {
        socket_unlock();
        return -1;
    }
    if (s->type == SOCK_DGRAM_K || s->type == SOCK_RAW_K) {
        s->peer_ip = addr->sin_addr;
        s->peer_port = ntoh16(addr->sin_port);
        s->connected = 1;
        socket_unlock();
        return 0;
    }
    if (s->type != SOCK_STREAM_K || active_tcp_socket >= 0) {
        socket_unlock();
        return -1;
    }
    active_tcp_socket = (int)sd_arg;
    socket_unlock();

    int ret = net_tcp_connect(addr->sin_addr, ntoh16(addr->sin_port));
    socket_lock();
    s = socket_get((int)sd_arg);
    if (ret < 0) {
        if (active_tcp_socket == (int)sd_arg)
            active_tcp_socket = -1;
        socket_unlock();
        return -1;
    }
    if (s)
        s->connected = 1;
    socket_unlock();
    return 0;
}

static int sys_send(uint32_t sd_arg, uint32_t buf, uint32_t len, uint32_t flags, uint32_t e) {
    (void)flags; (void)e;
    if (!user_range_ok(buf, len))
        return -1;
    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s || !s->connected) {
        socket_unlock();
        return -1;
    }
    int type = s->type;
    uint16_t local_port = s->local_port;
    uint32_t peer_ip = s->peer_ip;
    uint16_t peer_port = s->peer_port;
    int tcp_ok = active_tcp_socket == (int)sd_arg;
    socket_unlock();
    if (type == SOCK_STREAM_K)
        return tcp_ok ? net_tcp_send((const void *)(uintptr_t)buf, (size_t)len) : -1;
    if (type == SOCK_DGRAM_K)
        return net_udp_send(peer_ip, local_port, peer_port, (const void *)(uintptr_t)buf, (size_t)len);
    if (type == SOCK_RAW_K)
        return net_icmp_send_echo(peer_ip, local_port, 1, (const void *)(uintptr_t)buf, (size_t)len);
    return -1;
}

static int sys_recv(uint32_t sd_arg, uint32_t buf, uint32_t len, uint32_t flags, uint32_t e) {
    (void)flags; (void)e;
    if (!user_range_ok(buf, len))
        return -1;
    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s || !s->connected) {
        socket_unlock();
        return -1;
    }
    int type = s->type;
    uint16_t local_port = s->local_port;
    uint32_t peer_ip = s->peer_ip;
    int tcp_ok = active_tcp_socket == (int)sd_arg;
    socket_unlock();
    if (type == SOCK_STREAM_K)
        return tcp_ok ? net_tcp_recv((void *)(uintptr_t)buf, (size_t)len) : -1;
    if (type == SOCK_DGRAM_K)
        return net_udp_recv(local_port, 0, 0, (void *)(uintptr_t)buf, (size_t)len);
    if (type == SOCK_RAW_K)
        return net_icmp_recv_echo(peer_ip, local_port, 0, (void *)(uintptr_t)buf, (size_t)len);
    return -1;
}

static int sys_bind(uint32_t sd_arg, uint32_t addr_arg, uint32_t addrlen,
                    uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (addrlen < sizeof(struct k_sockaddr_in) ||
        !user_range_ok(addr_arg, sizeof(struct k_sockaddr_in)))
        return -1;
    struct k_sockaddr_in *addr = (struct k_sockaddr_in *)(uintptr_t)addr_arg;
    if (addr->sin_family != AF_INET_K)
        return -1;
    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s) {
        socket_unlock();
        return -1;
    }
    s->local_port = ntoh16(addr->sin_port);
    socket_unlock();
    return 0;
}

static int sys_sendto(uint32_t sd_arg, uint32_t buf, uint32_t len,
                      uint32_t addr_arg, uint32_t addrlen) {
    if (!user_range_ok(buf, len) || addrlen < sizeof(struct k_sockaddr_in) ||
        !user_range_ok(addr_arg, sizeof(struct k_sockaddr_in)))
        return -1;
    struct k_sockaddr_in *addr = (struct k_sockaddr_in *)(uintptr_t)addr_arg;
    if (addr->sin_family != AF_INET_K)
        return -1;
    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s) {
        socket_unlock();
        return -1;
    }
    int type = s->type;
    uint16_t local_port = s->local_port;
    socket_unlock();
    if (type == SOCK_DGRAM_K)
        return net_udp_send(addr->sin_addr, local_port, ntoh16(addr->sin_port),
                            (const void *)(uintptr_t)buf, (size_t)len);
    if (type == SOCK_RAW_K)
        return net_icmp_send_echo(addr->sin_addr, local_port, 1,
                                  (const void *)(uintptr_t)buf, (size_t)len);
    return -1;
}

static int sys_recvfrom(uint32_t sd_arg, uint32_t buf, uint32_t len,
                        uint32_t addr_arg, uint32_t addrlen) {
    if (!user_range_ok(buf, len))
        return -1;
    if (addr_arg && (addrlen < sizeof(struct k_sockaddr_in) ||
        !user_range_ok(addr_arg, sizeof(struct k_sockaddr_in))))
        return -1;
    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s) {
        socket_unlock();
        return -1;
    }
    int type = s->type;
    uint16_t local_port = s->local_port;
    uint32_t peer_ip = s->peer_ip;
    socket_unlock();
    uint32_t src_ip = 0;
    uint16_t src_port = 0;
    int ret;
    if (type == SOCK_DGRAM_K) {
        ret = net_udp_recv(local_port, &src_ip, &src_port, (void *)(uintptr_t)buf, (size_t)len);
    } else if (type == SOCK_RAW_K) {
        ret = net_icmp_recv_echo(peer_ip, local_port, 0, (void *)(uintptr_t)buf, (size_t)len);
        src_ip = peer_ip;
    } else {
        return -1;
    }
    if (ret >= 0 && addr_arg) {
        struct k_sockaddr_in *addr = (struct k_sockaddr_in *)(uintptr_t)addr_arg;
        addr->sin_family = AF_INET_K;
        addr->sin_port = ntoh16(src_port);
        addr->sin_addr = src_ip;
    }
    return ret;
}

static int sys_closesocket(uint32_t sd_arg, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    socket_lock();
    struct socket_entry *s = socket_get((int)sd_arg);
    if (!s) {
        socket_unlock();
        return -1;
    }
    int was_active = active_tcp_socket == (int)sd_arg;
    s->used = 0;
    s->connected = 0;
    if (was_active)
        active_tcp_socket = -1;
    socket_unlock();
    if (was_active)
        net_tcp_close();
    return 0;
}

static int sys_dns_resolve(uint32_t host_arg, uint32_t ip_out_arg, uint32_t c,
                           uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    const char *host = (const char *)(uintptr_t)host_arg;
    if (!user_string_ok(host) || !user_range_ok(ip_out_arg, sizeof(uint32_t)))
        return -1;
    return net_dns_resolve(host, (uint32_t *)(uintptr_t)ip_out_arg);
}

static int sys_netinfo(uint32_t mac_arg, uint32_t ip_arg, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (mac_arg && !user_range_ok(mac_arg, 6))
        return -1;
    if (ip_arg && !user_range_ok(ip_arg, sizeof(uint32_t)))
        return -1;
    if (mac_arg) {
        uint8_t *mac = (uint8_t *)(uintptr_t)mac_arg;
        for (int i = 0; i < 6; i++)
            mac[i] = net_mac[i];
    }
    if (ip_arg)
        *(uint32_t *)(uintptr_t)ip_arg = net_ip;
    return 0;
}

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int sys_open_console_aware(uint32_t path_arg, uint32_t flags, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    const char *path = (const char *)(uintptr_t)path_arg;
    if (!user_string_ok(path))
        return -1;
    if (current_task && current_task->console_silent && path && streq(path, "/dev/console"))
        return vfs_open_flags("/dev/null", (int)flags);
    return vfs_open_flags(path, (int)flags);
}

/* Thread entry wrapper: enters ring3 at the user function, then exits. */
struct thread_info {
    uint32_t func_addr;
    uint32_t stack_top;
};
static struct thread_info thread_infos[MAX_TASKS];
static int process_thread_count[MAX_TASKS];

void syscall_reset_process(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS) return;
    process_thread_count[task_id] = 0;
}

static void thread_trampoline(void) {
    int id = current_task->id;
    uint32_t func = thread_infos[id].func_addr;
    uint32_t stack = thread_infos[id].stack_top;
    user_enter(func, stack);
}

static int sys_spawn(uint32_t func_addr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_range_ok(func_addr, 1))
        return -1;
    int owner = current_task ? current_task->fd_owner : 0;
    if (owner < 0 || owner >= MAX_TASKS)
        return -1;

    int slot = ++process_thread_count[owner];
    uint32_t user_stack = USER_DEFAULT_STACK_TOP - (uint32_t)(slot * 0x4000);
    if (user_stack < 0x001C0000)
        return -1;

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

static int sys_yield(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    task_yield();
    return 0;
}

static int sys_join(uint32_t tid_arg, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
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

static int sys_sleep(uint32_t ms, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    timer_sleep_ms(ms);
    return 0;
}

static int sys_kill(uint32_t pid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return task_kill((int)pid);
}

static int sys_getpid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return task_get_pid();
}

static int sys_gettid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return task_get_tid();
}

static int sys_chdir(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_string_ok((const char *)(uintptr_t)path))
        return -1;
    return vfs_chdir((const char *)(uintptr_t)path);
}

static int sys_getcwd(uint32_t buf, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!user_range_ok(buf, size))
        return -1;
    return vfs_getcwd((char *)(uintptr_t)buf, (size_t)size);
}

static int sys_waitpid(uint32_t pid, uint32_t status, uint32_t options, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (status && !user_range_ok(status, sizeof(int)))
        return -1;
    return task_wait_pid((int)pid, (int *)(uintptr_t)status, (int)options);
}

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
    syscall_table[SYS_EXIT]  = sys_exit;
    syscall_table[SYS_OPEN]  = sys_open_console_aware;
    syscall_table[SYS_CLOSE] = sys_close;
    syscall_table[SYS_READ]  = sys_read;
    syscall_table[SYS_WRITE] = sys_write;
    syscall_table[SYS_DUP]   = sys_dup;
    syscall_table[SYS_DUP2]  = sys_dup2;
    syscall_table[SYS_STAT]  = sys_stat;
    syscall_table[SYS_GETDENTS] = sys_getdents;
    syscall_table[SYS_SPAWN_PROC] = sys_spawn_proc;
    syscall_table[SYS_PS] = sys_ps;
    syscall_table[SYS_REBOOT] = sys_reboot;
    syscall_table[SYS_MKDIR] = sys_mkdir;
    syscall_table[SYS_UNLINK] = sys_unlink;
    syscall_table[SYS_CREATE] = sys_create;
    syscall_table[SYS_SPAWN_PROC_ARGS] = sys_spawn_proc_args;
    syscall_table[SYS_LSEEK] = sys_lseek;
    syscall_table[SYS_RMDIR] = sys_rmdir;
    syscall_table[SYS_RENAME] = sys_rename;
    syscall_table[SYS_SOCKET] = sys_socket;
    syscall_table[SYS_CONNECT] = sys_connect;
    syscall_table[SYS_SEND] = sys_send;
    syscall_table[SYS_RECV] = sys_recv;
    syscall_table[SYS_CLOSESOCKET] = sys_closesocket;
    syscall_table[SYS_DNS_RESOLVE] = sys_dns_resolve;
    syscall_table[SYS_BIND] = sys_bind;
    syscall_table[SYS_SENDTO] = sys_sendto;
    syscall_table[SYS_RECVFROM] = sys_recvfrom;
    syscall_table[SYS_NETINFO] = sys_netinfo;
    syscall_table[SYS_SPAWN] = sys_spawn;
    syscall_table[SYS_YIELD] = sys_yield;
    syscall_table[SYS_JOIN]  = sys_join;
    syscall_table[SYS_SLEEP] = sys_sleep;
    syscall_table[SYS_KILL]  = sys_kill;
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_GETTID] = sys_gettid;
    syscall_table[SYS_CHDIR]  = sys_chdir;
    syscall_table[SYS_GETCWD] = sys_getcwd;
    syscall_table[SYS_WAITPID] = sys_waitpid;
    serial_puts("[syscall] VFS-backed syscalls ready\n");
}
