#include "libc.h"

static int write_all(int fd, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = write(fd, buf + off, (size_t)(len - off));
        if (n < 0)
            return -1;
        if (n == 0) {
            yield();
            continue;
        }
        off += n;
    }
    return 0;
}

static int copy_fd(int fd, int retry_empty_pipe) {
    char buf[128];
    int empty_reads = 0;
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            if (write_all(1, buf, n) < 0)
                return -1;
            empty_reads = 0;
            continue;
        }
        if (n == 0)
            return 0;
        if (retry_empty_pipe && empty_reads++ < 20000) {
            yield();
            continue;
        }
        return -1;
    }
}

int main(int argc, char **argv) {
    if (argc <= 1)
        return copy_fd(0, 1) < 0 ? 1 : 0;

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: cannot open %s\n", argv[i]);
            rc = 1;
            continue;
        }
        if (copy_fd(fd, 0) < 0) {
            printf("cat: read failed %s\n", argv[i]);
            rc = 1;
        }
        close(fd);
    }
    return rc;
}
