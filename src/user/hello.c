static int sys_open(const char *path) {
    int fd;
    __asm__ volatile(
        "movl $2, %%eax\n"
        "int  $0x80\n"
        : "=a"(fd)
        : "a"(2), "b"(path)
        : "memory"
    );
    return fd;
}

static int sys_write(int fd, const char *buf, int len) {
    int ret;
    __asm__ volatile(
        "movl $5, %%eax\n"
        "int  $0x80\n"
        : "=a"(ret)
        : "a"(5), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

static void sys_close(int fd) {
    __asm__ volatile(
        "movl $3, %%eax\n"
        "int  $0x80\n"
        :
        : "a"(3), "b"(fd)
        : "memory"
    );
}

static void sys_exit(int code) {
    __asm__ volatile(
        "movl $1, %%eax\n"
        "int  $0x80\n"
        :
        : "a"(1), "b"(code)
        : "memory"
    );
    __builtin_unreachable();
}

__attribute__((section(".text.entry")))
void _start(void) {
    int fd = sys_open("/dev/serial");
    if (fd >= 0) {
        sys_write(fd, "Hello via /dev/serial!\n", 23);
        sys_close(fd);
    }

    fd = sys_open("/dev/null");
    if (fd >= 0) {
        sys_write(fd, "this goes nowhere\n", 18);
        sys_close(fd);
    }

    sys_exit(0);
}
