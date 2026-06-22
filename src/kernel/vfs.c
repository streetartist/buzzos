#include "vfs.h"
#include "serial.h"

/* ------------------------------------------------------------------ */
/*  Global fd table                                                     */
/* ------------------------------------------------------------------ */

#define MAX_FD    32
#define MAX_DEV   16
#define MAX_RAMFS 16

static vnode_t *fd_table[MAX_FD];
static int      fd_next = 0;   /* 0,1,2 reserved for stdin/stdout/stderr */

/* ------------------------------------------------------------------ */
/*  devfs                                                               */
/* ------------------------------------------------------------------ */

static vnode_t *devfs_nodes[MAX_DEV];
static int      devfs_count;

void devfs_register(const char *name, const struct vnode_ops *ops,
                    void *data) {
    if (devfs_count >= MAX_DEV) return;
    static vnode_t storage[MAX_DEV];
    vnode_t *vn = &storage[devfs_count];
    vn->name = name;
    vn->ops  = ops;
    vn->data = data;
    devfs_nodes[devfs_count] = vn;
    devfs_count++;
}

/* ------------------------------------------------------------------ */
/*  ramfs                                                               */
/* ------------------------------------------------------------------ */

struct ramfs_file {
    const char    *name;
    const uint8_t *data;
    size_t         size;
};

static struct ramfs_file ramfs_files[MAX_RAMFS];
static int               ramfs_count;

void ramfs_register(const char *name, const uint8_t *data, size_t size) {
    if (ramfs_count >= MAX_RAMFS) return;
    ramfs_files[ramfs_count].name = name;
    ramfs_files[ramfs_count].data = data;
    ramfs_files[ramfs_count].size = size;
    ramfs_count++;
}

static const struct ramfs_file *ramfs_lookup(const char *name) {
    for (int i = 0; i < ramfs_count; i++) {
        const char *a = name, *b = ramfs_files[i].name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return &ramfs_files[i];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  vnode ops for ramfs (read-only stream)                              */
/* ------------------------------------------------------------------ */

struct ramfs_stream {
    const uint8_t *base;
    size_t         size;
    size_t         pos;
};

static int ramfs_open(vnode_t *vn) {
    return 0;
}

static int ramfs_read(vnode_t *vn, void *buf, size_t count) {
    struct ramfs_stream *s = (struct ramfs_stream *)vn->data;
    if (s->pos >= s->size) return 0;
    size_t n = s->size - s->pos;
    if (n > count) n = count;
    for (size_t i = 0; i < n; i++)
        ((uint8_t *)buf)[i] = s->base[s->pos + i];
    s->pos += n;
    return (int)n;
}

static int ramfs_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;  /* read-only */
}

static int ramfs_close(vnode_t *vn) {
    (void)vn;
    return 0;
}

static const struct vnode_ops ramfs_ops = {
    .open  = ramfs_open,
    .read  = ramfs_read,
    .write = ramfs_write,
    .close = ramfs_close,
};

/* ------------------------------------------------------------------ */
/*  devfs ops: /dev/serial                                              */
/* ------------------------------------------------------------------ */

static int serial_open(vnode_t *vn)  { (void)vn; return 0; }
static int serial_close(vnode_t *vn) { (void)vn; return 0; }

static int serial_read_dev(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return 0;  /* no input */
}

static int serial_write_dev(vnode_t *vn, const void *buf, size_t count) {
    (void)vn;
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++) serial_putc((char)p[i]);
    return (int)count;
}

static const struct vnode_ops serial_dev_ops = {
    .open  = serial_open,
    .read  = serial_read_dev,
    .write = serial_write_dev,
    .close = serial_close,
};

/* ------------------------------------------------------------------ */
/*  devfs ops: /dev/null                                                */
/* ------------------------------------------------------------------ */

static int null_open(vnode_t *vn)   { (void)vn; return 0; }
static int null_close(vnode_t *vn)  { (void)vn; return 0; }

static int null_read(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return 0;  /* EOF */
}

static int null_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf;
    return (int)count;  /* succeed, discard */
}

static const struct vnode_ops null_dev_ops = {
    .open  = null_open,
    .read  = null_read,
    .write = null_write,
    .close = null_close,
};

/* ------------------------------------------------------------------ */
/*  VFS open / read / write / close                                     */
/* ------------------------------------------------------------------ */

static int alloc_fd(vnode_t *vn) {
    for (int i = 0; i < MAX_FD; i++) {
        if (fd_table[i] == 0) {
            fd_table[i] = vn;
            return i;
        }
    }
    return -1;
}

int vfs_open(const char *path) {
    /* devfs lookup: "/dev/..." */
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/') {
        const char *name = path + 5;
        for (int i = 0; i < devfs_count; i++) {
            const char *a = name, *b = devfs_nodes[i]->name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == 0 && *b == 0) {
                vnode_t *vn = devfs_nodes[i];
                if (vn->ops->open) vn->ops->open(vn);
                int fd = alloc_fd(vn);
                return fd;
            }
        }
    }

    /* ramfs lookup */
    const struct ramfs_file *rf = ramfs_lookup(path);
    if (rf) {
        static struct ramfs_stream streams[16];
        static int si = 0;
        struct ramfs_stream *s = &streams[si++ % 16];
        s->base = rf->data;
        s->size = rf->size;
        s->pos  = 0;

        static vnode_t ramfs_vnodes[16];
        static int vi = 0;
        vnode_t *vn = &ramfs_vnodes[vi++ % 16];
        vn->name = path;
        vn->ops  = &ramfs_ops;
        vn->data = s;
        vn->ops->open(vn);
        return alloc_fd(vn);
    }

    return -1;
}

int vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd]) return -1;
    return fd_table[fd]->ops->read(fd_table[fd], buf, count);
}

int vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd]) return -1;
    return fd_table[fd]->ops->write(fd_table[fd], buf, count);
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd]) return -1;
    int ret = 0;
    if (fd_table[fd]->ops->close)
        ret = fd_table[fd]->ops->close(fd_table[fd]);
    fd_table[fd] = 0;
    return ret;
}

/* ------------------------------------------------------------------ */
/*  Initialise                                                           */
/* ------------------------------------------------------------------ */

void vfs_init(void) {
    for (int i = 0; i < MAX_FD; i++) fd_table[i] = 0;

    devfs_register("serial", &serial_dev_ops, 0);
    devfs_register("null",   &null_dev_ops,   0);

    serial_puts("[vfs] devfs: /dev/serial, /dev/null\n");
}

void vfs_ls(void (*putc)(char)) {
    const char *s = "\n/dev/serial\n/dev/null\n/init\n";
    while (*s) putc(*s++);
    for (int i = 0; i < ramfs_count; i++) {
        const char *n = ramfs_files[i].name;
        putc('\n'); while (*n) putc(*n++);
    }
    putc('\n');
}

int vfs_cat(const char *path, void (*putc)(char)) {
    int fd = vfs_open(path);
    if (fd < 0) return -1;
    char buf[128];
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
        for (int i = 0; i < n; i++) putc(buf[i]);
    vfs_close(fd);
    return 0;
}
