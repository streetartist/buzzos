#include "vfs.h"
#include "fs/minifs.h"
#include "keyboard.h"
#include "serial.h"
#include "task.h"

#define MAX_FD        32
#define MAX_DEV       16
#define FS_MAX_NODES  64
#define FS_NAME_LEN   24
#define FS_FILE_BUFSZ 2048
#define MAX_MOUNTS    8
#define MAX_PIPES     16
#define PIPE_BUFSZ    512

enum {
    PIPE_READ_END = 1,
    PIPE_WRITE_END = 2,
};

enum node_type {
    NODE_FREE = 0,
    NODE_DIR,
    NODE_FILE,
    NODE_DEV,
};

struct fs_node {
    int used;
    enum node_type type;
    int readonly;
    char name[FS_NAME_LEN];
    struct fs_node *parent;
    struct fs_node *first_child;
    struct fs_node *next_sibling;
    const uint8_t *ro_data;
    uint8_t data[FS_FILE_BUFSZ];
    size_t size;
    const struct vnode_ops *dev_ops;
    void *dev_data;
};

struct file_stream {
    struct fs_node *node;
    size_t pos;
    uint16_t minifs_ino;
    int pipe_idx;
    int pipe_end;
};

struct open_file {
    int used;
    int refs;
    int flags;
    vnode_t vnode;
    struct file_stream stream;
};

struct vfs_mount {
    int used;
    const struct fs_ops *ops;
    char path[FS_NAME_LEN];
    int path_len;
};

struct fs_ops {
    int (*open)(const char *abs, const char *rel, int flags, struct open_file *of);
    int (*create)(const char *abs, const char *rel);
    int (*mkdir)(const char *abs, const char *rel);
    int (*unlink)(const char *abs, const char *rel);
    int (*rmdir)(const char *abs, const char *rel);
    int (*rename)(const char *old_abs, const char *old_rel,
                  const char *new_abs, const char *new_rel);
    int (*stat)(const char *abs, const char *rel, struct stat *st);
    int (*is_dir)(const char *abs, const char *rel);
    int (*ls)(const char *abs, const char *rel, void (*putc)(char));
};

struct pipe_obj {
    int used;
    int readers;
    int writers;
    size_t head;
    size_t tail;
    size_t count;
    uint8_t data[PIPE_BUFSZ];
};

static struct fs_node nodes[FS_MAX_NODES];
static struct fs_node *root_node;
static struct fs_node *dev_node;
static struct fs_node *fs_node;

static struct open_file open_files[MAX_TASKS][MAX_FD];
static int fd_open_file[MAX_TASKS][MAX_FD];
static int fd_used[MAX_TASKS][MAX_FD];
static struct vfs_mount mounts[MAX_MOUNTS];
static struct pipe_obj pipes[MAX_PIPES];
static volatile int vfs_locked;
static uint32_t vfs_irq_flags;

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
}

static void vfs_lock(void) {
    uint32_t flags = irq_save();
    while (__sync_lock_test_and_set(&vfs_locked, 1)) {
        __asm__ volatile("pause");
    }
    vfs_irq_flags = flags;
}

static void vfs_unlock(void) {
    uint32_t flags = vfs_irq_flags;
    __sync_lock_release(&vfs_locked);
    irq_restore(flags);
}

static int nameeq(const char *name, const char *part, int len) {
    int i = 0;
    while (i < len && name[i] && name[i] == part[i]) i++;
    return i == len && name[i] == 0;
}

static int normalize_path(const char *in, char *out, int out_sz) {
    char tmp[128];
    int n = 0;

    if (!in || !in[0])
        in = ".";
    if (in[0] == '/') {
        tmp[n++] = '/';
    } else {
        char cwd[128];
        if (task_get_cwd(cwd, sizeof(cwd)) < 0)
            return -1;
        for (int i = 0; cwd[i] && n < (int)sizeof(tmp) - 1; i++)
            tmp[n++] = cwd[i];
        if (n > 1 && tmp[n - 1] != '/' && n < (int)sizeof(tmp) - 1)
            tmp[n++] = '/';
    }
    for (int i = 0; in[i] && n < (int)sizeof(tmp) - 1; i++)
        tmp[n++] = in[i];
    tmp[n] = 0;

    int len = 1;
    out[0] = '/';
    out[1] = 0;

    const char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != '/') p++;
        int part_len = (int)(p - start);
        if (part_len == 1 && start[0] == '.')
            continue;
        if (part_len == 2 && start[0] == '.' && start[1] == '.') {
            if (len > 1) {
                if (out[len - 1] == '/') len--;
                while (len > 1 && out[len - 1] != '/') len--;
                out[len] = 0;
            }
            continue;
        }
        if (len > 1 && out[len - 1] != '/') {
            if (len >= out_sz - 1) return -1;
            out[len++] = '/';
        }
        for (int i = 0; i < part_len; i++) {
            if (len >= out_sz - 1) return -1;
            out[len++] = start[i];
        }
        out[len] = 0;
    }
    if (len > 1 && out[len - 1] == '/')
        out[len - 1] = 0;
    return 0;
}

static void copy_name(char *dst, const char *src, int len) {
    int i = 0;
    while (i < len && i < FS_NAME_LEN - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int str_len(const char *s) {
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int path_mount_match(const char *path, const char *mount_path, int mount_len) {
    if (mount_len == 1 && mount_path[0] == '/')
        return path && path[0] == '/';
    if (!path || mount_len <= 0)
        return 0;
    for (int i = 0; i < mount_len; i++) {
        if (path[i] != mount_path[i])
            return 0;
    }
    return path[mount_len] == 0 || path[mount_len] == '/';
}

static struct vfs_mount *find_mount(const char *abs) {
    struct vfs_mount *best = 0;
    int best_len = -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts[i].used)
            continue;
        if (path_mount_match(abs, mounts[i].path, mounts[i].path_len) &&
            mounts[i].path_len > best_len) {
            best = &mounts[i];
            best_len = mounts[i].path_len;
        }
    }
    return best;
}

static const char *mount_relpath(struct vfs_mount *mnt, const char *abs) {
    if (!mnt)
        return 0;
    if (mnt->path_len == 1 && mnt->path[0] == '/')
        return abs;
    return abs[mnt->path_len] ? abs + mnt->path_len : "/";
}

static int add_mount(const char *path, const struct fs_ops *ops) {
    int len = str_len(path);
    if (len <= 0 || len >= FS_NAME_LEN || !ops)
        return -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].used && nameeq(mounts[i].path, path, len))
            return -1;
    }
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts[i].used) {
            mounts[i].used = 1;
            mounts[i].ops = ops;
            mounts[i].path_len = len;
            copy_name(mounts[i].path, path, len);
            return 0;
        }
    }
    return -1;
}

static struct fs_node *alloc_node(enum node_type type, const char *name, int len) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            struct fs_node *n = &nodes[i];
            n->used = 1;
            n->type = type;
            n->readonly = 0;
            n->parent = 0;
            n->first_child = 0;
            n->next_sibling = 0;
            n->ro_data = 0;
            n->size = 0;
            n->dev_ops = 0;
            n->dev_data = 0;
            copy_name(n->name, name, len);
            return n;
        }
    }
    return 0;
}

static void add_child(struct fs_node *parent, struct fs_node *child) {
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

static struct fs_node *find_child(struct fs_node *dir, const char *name, int len) {
    if (!dir || dir->type != NODE_DIR) return 0;
    for (struct fs_node *n = dir->first_child; n; n = n->next_sibling) {
        if (nameeq(n->name, name, len))
            return n;
    }
    return 0;
}

static struct fs_node *resolve_path(const char *path) {
    if (!path || path[0] != '/') return 0;
    struct fs_node *cur = root_node;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);
        if (len > 0) {
            cur = find_child(cur, start, len);
            if (!cur) return 0;
        }
        while (*p == '/') p++;
    }
    return cur;
}

static struct fs_node *resolve_parent(const char *path, const char **leaf, int *leaf_len) {
    if (!path || path[0] != '/') return 0;
    const char *last = 0;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        last = p;
        while (*p && *p != '/') p++;
    }
    if (!last) return 0;

    *leaf = last;
    p = last;
    while (*p && *p != '/') p++;
    *leaf_len = (int)(p - last);

    struct fs_node *cur = root_node;
    p = path;
    while (*p == '/') p++;
    while (p < last) {
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);
        while (*p == '/') p++;
        if (p > last) break;
        if (len > 0) {
            cur = find_child(cur, start, len);
            if (!cur || cur->type != NODE_DIR) return 0;
        }
    }
    return cur;
}

static int unlink_child(struct fs_node *n) {
    struct fs_node *parent = n->parent;
    if (!parent) return -1;
    struct fs_node **pp = &parent->first_child;
    while (*pp) {
        if (*pp == n) {
            *pp = n->next_sibling;
            n->used = 0;
            return 0;
        }
        pp = &(*pp)->next_sibling;
    }
    return -1;
}

static int detach_child(struct fs_node *n) {
    struct fs_node *parent = n->parent;
    if (!parent) return -1;
    struct fs_node **pp = &parent->first_child;
    while (*pp) {
        if (*pp == n) {
            *pp = n->next_sibling;
            n->parent = 0;
            n->next_sibling = 0;
            return 0;
        }
        pp = &(*pp)->next_sibling;
    }
    return -1;
}

static int current_fd_owner(void) {
    if (!current_task) return 0;
    if (current_task->id < 0 || current_task->id >= MAX_TASKS) return 0;
    if (current_task->fd_owner < 0 || current_task->fd_owner >= MAX_TASKS)
        return current_task->id;
    return current_task->fd_owner;
}

static int valid_fd_owner(int owner) {
    return owner >= 0 && owner < MAX_TASKS;
}

static int alloc_open_file(int owner);
static const struct vnode_ops file_ops;
static const struct vnode_ops dir_ops;
static const struct vnode_ops minifs_file_ops;
static const struct vnode_ops minifs_dir_ops;
static const struct vnode_ops pipe_ops;
static const struct fs_ops ramfs_ops;
static const struct fs_ops minifs_ops;

static int alloc_fd_slot(int owner) {
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_used[owner][i])
            return i;
    }
    return -1;
}

static struct vfs_mount *mount_for_path(const char *abs, const char **rel_out) {
    struct vfs_mount *mnt = find_mount(abs);
    if (!mnt || !mnt->ops)
        return 0;
    if (rel_out)
        *rel_out = mount_relpath(mnt, abs);
    return mnt;
}

static int open_can_read(int flags) {
    return (flags & 3) != O_WRONLY;
}

static int open_can_write(int flags) {
    int mode = flags & 3;
    return mode == O_WRONLY || mode == O_RDWR;
}

static struct fs_node *create_file_locked(const char *abs) {
    const char *leaf;
    int leaf_len;
    struct fs_node *parent = resolve_parent(abs, &leaf, &leaf_len);
    if (!parent || parent->type != NODE_DIR || leaf_len <= 0)
        return 0;
    struct fs_node *old = find_child(parent, leaf, leaf_len);
    if (old)
        return old->type == NODE_FILE && !old->readonly ? old : 0;
    struct fs_node *n = alloc_node(NODE_FILE, leaf, leaf_len);
    if (!n)
        return 0;
    add_child(parent, n);
    return n;
}

static void init_open_file(struct open_file *of, int flags) {
    of->used = 1;
    of->refs = 1;
    of->flags = flags;
    of->vnode.name = 0;
    of->vnode.ops = 0;
    of->vnode.data = 0;
    of->stream.node = 0;
    of->stream.pos = 0;
    of->stream.minifs_ino = 0;
    of->stream.pipe_idx = -1;
    of->stream.pipe_end = 0;
}

static int ramfs_fs_open(const char *abs, const char *rel, int flags, struct open_file *of) {
    (void)rel;
    struct fs_node *n = resolve_path(abs);
    if (!n && (flags & O_CREAT))
        n = create_file_locked(abs);
    if (!n)
        return -1;
    if (n->type == NODE_DIR && open_can_write(flags))
        return -1;
    if ((flags & O_TRUNC) && (!open_can_write(flags) || n->type != NODE_FILE || n->readonly))
        return -1;

    init_open_file(of, flags);
    of->vnode.name = n->name;
    if (n->type == NODE_DEV) {
        of->vnode.ops = n->dev_ops;
        of->vnode.data = n->dev_data;
    } else if (n->type == NODE_DIR) {
        of->stream.node = n;
        of->vnode.ops = &dir_ops;
        of->vnode.data = &of->stream;
    } else {
        if (flags & O_TRUNC)
            n->size = 0;
        of->stream.node = n;
        of->stream.pos = (flags & O_APPEND) ? n->size : 0;
        of->vnode.ops = &file_ops;
        of->vnode.data = &of->stream;
    }
    return 0;
}

static int minifs_fs_open(const char *abs, const char *rel, int flags, struct open_file *of) {
    (void)abs;
    if ((flags & O_CREAT) && minifs_create(rel) < 0)
        return -1;
    uint16_t ino;
    if (minifs_open(rel, &ino) < 0)
        return -1;
    int is_dir = minifs_is_dir_ino(ino);
    if (is_dir && open_can_write(flags))
        return -1;
    if ((flags & O_TRUNC) && (!open_can_write(flags) || minifs_truncate(rel) < 0))
        return -1;

    init_open_file(of, flags);
    of->vnode.name = "minifs";
    of->vnode.ops = is_dir ? &minifs_dir_ops : &minifs_file_ops;
    of->stream.minifs_ino = ino;
    if ((flags & O_APPEND) && !is_dir)
        minifs_size_ino(ino, &of->stream.pos);
    of->vnode.data = &of->stream;
    return 0;
}

static int open_abs_for_owner(int owner, const char *abs, int flags) {
    if (!valid_fd_owner(owner))
        return -1;

    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->open)
        return -1;

    int fd = alloc_fd_slot(owner);
    int of_idx = alloc_open_file(owner);
    if (fd < 0 || of_idx < 0)
        return -1;

    struct open_file *of = &open_files[owner][of_idx];
    if (mnt->ops->open(abs, rel, flags, of) < 0)
        return -1;

    if (of->vnode.ops && of->vnode.ops->open)
        of->vnode.ops->open(&of->vnode);
    fd_used[owner][fd] = 1;
    fd_open_file[owner][fd] = of_idx;
    return fd;
}

static int alloc_open_file(int owner) {
    for (int i = 0; i < MAX_FD; i++) {
        if (!open_files[owner][i].used)
            return i;
    }
    return -1;
}

static struct open_file *fd_to_open_file(int owner, int fd) {
    if (fd < 0 || fd >= MAX_FD || !fd_used[owner][fd])
        return 0;
    int of = fd_open_file[owner][fd];
    if (of < 0 || of >= MAX_FD || !open_files[owner][of].used)
        return 0;
    return &open_files[owner][of];
}

static int close_fd_locked(int owner, int fd) {
    struct open_file *of = fd_to_open_file(owner, fd);
    if (!of)
        return -1;

    fd_used[owner][fd] = 0;
    fd_open_file[owner][fd] = -1;

    if (--of->refs > 0)
        return 0;

    int ret = 0;
    if (of->vnode.ops && of->vnode.ops->close)
        ret = of->vnode.ops->close(&of->vnode);
    of->used = 0;
    of->refs = 0;
    of->flags = 0;
    of->vnode.name = 0;
    of->vnode.ops = 0;
    of->vnode.data = 0;
    of->stream.node = 0;
    of->stream.pos = 0;
    of->stream.minifs_ino = 0;
    of->stream.pipe_idx = -1;
    of->stream.pipe_end = 0;
    return ret;
}

/* ------------------------------------------------------------------ */
/*  File vnode ops                                                     */
/* ------------------------------------------------------------------ */

static uint32_t node_type_to_dirent(enum node_type type);
static void copy_dirent_name(char *dst, const char *src);

static int file_open(vnode_t *vn) { (void)vn; return 0; }
static int file_close(vnode_t *vn) { (void)vn; return 0; }

static int file_read(vnode_t *vn, void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    struct fs_node *n = s->node;
    if (s->pos >= n->size) return 0;
    size_t avail = n->size - s->pos;
    if (count > avail) count = avail;
    const uint8_t *src = n->readonly ? n->ro_data : n->data;
    for (size_t i = 0; i < count; i++)
        ((uint8_t *)buf)[i] = src[s->pos + i];
    s->pos += count;
    return (int)count;
}

static int file_write(vnode_t *vn, const void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    struct fs_node *n = s->node;
    if (n->readonly) return -1;
    if (s->pos > FS_FILE_BUFSZ) return -1;
    size_t room = FS_FILE_BUFSZ - s->pos;
    if (count > room) count = room;
    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
        n->data[s->pos + i] = src[i];
    s->pos += count;
    if (s->pos > n->size)
        n->size = s->pos;
    return (int)count;
}

static const struct vnode_ops file_ops = {
    .open = file_open,
    .read = file_read,
    .write = file_write,
    .close = file_close,
};

static int dir_open(vnode_t *vn) { (void)vn; return 0; }
static int dir_close(vnode_t *vn) { (void)vn; return 0; }
static int dir_read(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}
static int dir_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}

static int dir_getdents_vn(vnode_t *vn, struct dirent *ents, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    size_t max_entries = count / sizeof(struct dirent);
    if (!s || !s->node || s->node->type != NODE_DIR || max_entries == 0)
        return -1;

    struct fs_node *child = s->node->first_child;
    size_t skip = s->pos;
    while (child && skip > 0) {
        child = child->next_sibling;
        skip--;
    }

    size_t copied = 0;
    while (child && copied < max_entries) {
        ents[copied].d_type = node_type_to_dirent(child->type);
        ents[copied].d_size = (uint32_t)child->size;
        copy_dirent_name(ents[copied].d_name, child->name);
        copied++;
        s->pos++;
        child = child->next_sibling;
    }
    return (int)(copied * sizeof(struct dirent));
}

static const struct vnode_ops dir_ops = {
    .open = dir_open,
    .read = dir_read,
    .write = dir_write,
    .getdents = dir_getdents_vn,
    .close = dir_close,
};

static int minifs_vn_open(vnode_t *vn) { (void)vn; return 0; }
static int minifs_vn_close(vnode_t *vn) { (void)vn; return 0; }

static int minifs_vn_read(vnode_t *vn, void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    return minifs_read(s->minifs_ino, &s->pos, buf, count);
}

static int minifs_vn_write(vnode_t *vn, const void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    return minifs_write(s->minifs_ino, &s->pos, buf, count);
}

static int minifs_dir_read(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}

static int minifs_dir_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}

static int minifs_dir_getdents(vnode_t *vn, struct dirent *ents, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    return minifs_getdents(s->minifs_ino, &s->pos, ents, count);
}

static const struct vnode_ops minifs_file_ops = {
    .open = minifs_vn_open,
    .read = minifs_vn_read,
    .write = minifs_vn_write,
    .close = minifs_vn_close,
};

static const struct vnode_ops minifs_dir_ops = {
    .open = minifs_vn_open,
    .read = minifs_dir_read,
    .write = minifs_dir_write,
    .getdents = minifs_dir_getdents,
    .close = minifs_vn_close,
};

/* ------------------------------------------------------------------ */
/*  Pipe vnode ops                                                     */
/* ------------------------------------------------------------------ */

static int pipe_open(vnode_t *vn) { (void)vn; return 0; }

static int pipe_close(vnode_t *vn) {
    struct file_stream *s = (struct file_stream *)vn->data;
    if (!s || s->pipe_idx < 0 || s->pipe_idx >= MAX_PIPES)
        return -1;
    struct pipe_obj *p = &pipes[s->pipe_idx];
    if (!p->used)
        return -1;
    if (s->pipe_end == PIPE_READ_END && p->readers > 0)
        p->readers--;
    if (s->pipe_end == PIPE_WRITE_END && p->writers > 0)
        p->writers--;
    if (p->readers == 0 && p->writers == 0)
        p->used = 0;
    return 0;
}

static int pipe_read(vnode_t *vn, void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    if (!s || s->pipe_end != PIPE_READ_END || s->pipe_idx < 0 || s->pipe_idx >= MAX_PIPES)
        return -1;
    struct pipe_obj *p = &pipes[s->pipe_idx];
    if (!p->used)
        return -1;
    uint8_t *out = (uint8_t *)buf;
    size_t done = 0;
    while (done < count && p->count > 0) {
        out[done++] = p->data[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUFSZ;
        p->count--;
    }
    return (int)done;
}

static int pipe_write(vnode_t *vn, const void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    if (!s || s->pipe_end != PIPE_WRITE_END || s->pipe_idx < 0 || s->pipe_idx >= MAX_PIPES)
        return -1;
    struct pipe_obj *p = &pipes[s->pipe_idx];
    if (!p->used || p->readers == 0)
        return -1;
    const uint8_t *in = (const uint8_t *)buf;
    size_t done = 0;
    while (done < count && p->count < PIPE_BUFSZ) {
        p->data[p->head] = in[done++];
        p->head = (p->head + 1) % PIPE_BUFSZ;
        p->count++;
    }
    return (int)done;
}

static const struct vnode_ops pipe_ops = {
    .open = pipe_open,
    .read = pipe_read,
    .write = pipe_write,
    .close = pipe_close,
};

/* ------------------------------------------------------------------ */
/*  Device vnode ops                                                   */
/* ------------------------------------------------------------------ */

static int serial_open(vnode_t *vn)  { (void)vn; return 0; }
static int serial_close(vnode_t *vn) { (void)vn; return 0; }

static int serial_read_dev(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return 0;
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

extern void vga_putc(char c);

static int console_open(vnode_t *vn)  { (void)vn; return 0; }
static int console_close(vnode_t *vn) { (void)vn; return 0; }

static int console_read(vnode_t *vn, void *buf, size_t count) {
    (void)vn;
    uint8_t *out = (uint8_t *)buf;
    size_t n = 0;
    while (n < count) {
        int c = keyboard_getchar();
        if (c < 0)
            break;
        out[n++] = (uint8_t)c;
    }
    return (int)n;
}

static int console_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn;
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        serial_putc((char)p[i]);
        vga_putc((char)p[i]);
    }
    return (int)count;
}

static const struct vnode_ops console_dev_ops = {
    .open  = console_open,
    .read  = console_read,
    .write = console_write,
    .close = console_close,
};

static int null_open(vnode_t *vn)   { (void)vn; return 0; }
static int null_close(vnode_t *vn)  { (void)vn; return 0; }

static int null_read(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return 0;
}

static int null_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf;
    return (int)count;
}

static const struct vnode_ops null_dev_ops = {
    .open  = null_open,
    .read  = null_read,
    .write = null_write,
    .close = null_close,
};

/* ------------------------------------------------------------------ */

void devfs_register(const char *name, const struct vnode_ops *ops, void *data) {
    if (!dev_node) return;
    int len = 0;
    while (name[len]) len++;
    if (find_child(dev_node, name, len)) return;
    struct fs_node *n = alloc_node(NODE_DEV, name, len);
    if (!n) return;
    n->dev_ops = ops;
    n->dev_data = data;
    add_child(dev_node, n);
}

int vfs_open_flags(const char *path, int flags) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;

    vfs_lock();
    int owner = current_fd_owner();
    int fd = open_abs_for_owner(owner, abs, flags);
    vfs_unlock();
    return fd;
}

int vfs_open(const char *path) {
    return vfs_open_flags(path, O_RDONLY);
}

int vfs_read(int fd, void *buf, size_t count) {
    vfs_lock();
    int owner = current_fd_owner();
    struct open_file *of = fd_to_open_file(owner, fd);
    if (!of || !open_can_read(of->flags) || !of->vnode.ops || !of->vnode.ops->read) {
        vfs_unlock();
        return -1;
    }
    int ret = of->vnode.ops->read(&of->vnode, buf, count);
    vfs_unlock();
    return ret;
}

int vfs_write(int fd, const void *buf, size_t count) {
    vfs_lock();
    int owner = current_fd_owner();
    struct open_file *of = fd_to_open_file(owner, fd);
    if (!of || !open_can_write(of->flags) || !of->vnode.ops || !of->vnode.ops->write) {
        vfs_unlock();
        return -1;
    }
    if (of->flags & O_APPEND) {
        if (of->stream.node && of->stream.node->type == NODE_FILE) {
            of->stream.pos = of->stream.node->size;
        } else if (of->stream.minifs_ino) {
            size_t size = 0;
            if (minifs_size_ino(of->stream.minifs_ino, &size) < 0) {
                vfs_unlock();
                return -1;
            }
            of->stream.pos = size;
        }
    }
    int ret = of->vnode.ops->write(&of->vnode, buf, count);
    vfs_unlock();
    return ret;
}

int vfs_close(int fd) {
    vfs_lock();
    int owner = current_fd_owner();
    int ret = close_fd_locked(owner, fd);
    vfs_unlock();
    return ret;
}

int vfs_dup(int fd) {
    vfs_lock();
    int owner = current_fd_owner();
    struct open_file *of = fd_to_open_file(owner, fd);
    int newfd = alloc_fd_slot(owner);
    if (!of || newfd < 0) {
        vfs_unlock();
        return -1;
    }
    int of_idx = fd_open_file[owner][fd];
    of->refs++;
    fd_used[owner][newfd] = 1;
    fd_open_file[owner][newfd] = of_idx;
    vfs_unlock();
    return newfd;
}

int vfs_dup2(int oldfd, int newfd) {
    vfs_lock();
    int owner = current_fd_owner();
    struct open_file *of = fd_to_open_file(owner, oldfd);
    if (!of || newfd < 0 || newfd >= MAX_FD) {
        vfs_unlock();
        return -1;
    }
    if (oldfd == newfd) {
        vfs_unlock();
        return newfd;
    }
    if (fd_used[owner][newfd])
        close_fd_locked(owner, newfd);
    int of_idx = fd_open_file[owner][oldfd];
    of->refs++;
    fd_used[owner][newfd] = 1;
    fd_open_file[owner][newfd] = of_idx;
    vfs_unlock();
    return newfd;
}

int vfs_pipe(int fds[2]) {
    if (!fds)
        return -1;
    vfs_lock();
    int owner = current_fd_owner();
    if (!valid_fd_owner(owner)) {
        vfs_unlock();
        return -1;
    }

    int pipe_idx = -1;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].used) {
            pipe_idx = i;
            break;
        }
    }
    int rfd = alloc_fd_slot(owner);
    if (rfd < 0 || pipe_idx < 0) {
        vfs_unlock();
        return -1;
    }
    fd_used[owner][rfd] = 1;
    int wfd = alloc_fd_slot(owner);
    fd_used[owner][rfd] = 0;
    if (wfd < 0) {
        vfs_unlock();
        return -1;
    }
    int rof = alloc_open_file(owner);
    if (rof < 0) {
        vfs_unlock();
        return -1;
    }
    open_files[owner][rof].used = 1;
    int wof = alloc_open_file(owner);
    open_files[owner][rof].used = 0;
    if (wof < 0) {
        vfs_unlock();
        return -1;
    }

    struct pipe_obj *p = &pipes[pipe_idx];
    p->used = 1;
    p->readers = 1;
    p->writers = 1;
    p->head = 0;
    p->tail = 0;
    p->count = 0;

    struct open_file *r = &open_files[owner][rof];
    init_open_file(r, O_RDONLY);
    r->vnode.name = "pipe";
    r->vnode.ops = &pipe_ops;
    r->stream.pipe_idx = pipe_idx;
    r->stream.pipe_end = PIPE_READ_END;
    r->vnode.data = &r->stream;

    struct open_file *w = &open_files[owner][wof];
    init_open_file(w, O_WRONLY);
    w->vnode.name = "pipe";
    w->vnode.ops = &pipe_ops;
    w->stream.pipe_idx = pipe_idx;
    w->stream.pipe_end = PIPE_WRITE_END;
    w->vnode.data = &w->stream;

    fd_used[owner][rfd] = 1;
    fd_used[owner][wfd] = 1;
    fd_open_file[owner][rfd] = rof;
    fd_open_file[owner][wfd] = wof;
    fds[0] = rfd;
    fds[1] = wfd;
    vfs_unlock();
    return 0;
}

int vfs_lseek(int fd, int offset, int whence) {
    vfs_lock();
    int owner = current_fd_owner();
    struct open_file *of = fd_to_open_file(owner, fd);
    if (!of) {
        vfs_unlock();
        return -1;
    }

    size_t size = 0;
    if (of->stream.node) {
        if (of->stream.node->type != NODE_FILE) {
            vfs_unlock();
            return -1;
        }
        size = of->stream.node->size;
    } else if (of->stream.minifs_ino) {
        if (minifs_size_ino(of->stream.minifs_ino, &size) < 0) {
            vfs_unlock();
            return -1;
        }
    } else {
        vfs_unlock();
        return -1;
    }

    int base;
    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = (int)of->stream.pos;
    else if (whence == SEEK_END)
        base = (int)size;
    else {
        vfs_unlock();
        return -1;
    }

    int next = base + offset;
    if (next < 0) {
        vfs_unlock();
        return -1;
    }
    of->stream.pos = (size_t)next;
    vfs_unlock();
    return next;
}

static uint32_t node_type_to_dirent(enum node_type type) {
    if (type == NODE_DIR) return DT_DIR;
    if (type == NODE_FILE) return DT_REG;
    if (type == NODE_DEV) return DT_CHR;
    return DT_UNKNOWN;
}

static uint32_t node_type_to_mode(enum node_type type) {
    if (type == NODE_DIR) return S_IFDIR | 0755u;
    if (type == NODE_FILE) return S_IFREG | 0644u;
    if (type == NODE_DEV) return S_IFCHR | 0666u;
    return 0;
}

static int ramfs_create_path(const char *abs, const char *rel) {
    (void)rel;
    vfs_lock();
    struct fs_node *n = create_file_locked(abs);
    vfs_unlock();
    return n ? 0 : -1;
}

static int ramfs_mkdir_path(const char *abs, const char *rel) {
    (void)rel;
    vfs_lock();
    const char *leaf;
    int leaf_len;
    struct fs_node *parent = resolve_parent(abs, &leaf, &leaf_len);
    if (!parent || parent->type != NODE_DIR || leaf_len <= 0 ||
        find_child(parent, leaf, leaf_len)) {
        vfs_unlock();
        return -1;
    }
    struct fs_node *n = alloc_node(NODE_DIR, leaf, leaf_len);
    if (!n) {
        vfs_unlock();
        return -1;
    }
    add_child(parent, n);
    vfs_unlock();
    return 0;
}

static int ramfs_unlink_path(const char *abs, const char *rel) {
    (void)rel;
    vfs_lock();
    struct fs_node *n = resolve_path(abs);
    if (!n || n == root_node || n == dev_node || n->readonly || n->type != NODE_FILE) {
        vfs_unlock();
        return -1;
    }
    int ret = unlink_child(n);
    vfs_unlock();
    return ret;
}

static int ramfs_rmdir_path(const char *abs, const char *rel) {
    (void)rel;
    vfs_lock();
    struct fs_node *n = resolve_path(abs);
    if (!n || n == root_node || n == dev_node || n->readonly ||
        n->type != NODE_DIR || n->first_child) {
        vfs_unlock();
        return -1;
    }
    int ret = unlink_child(n);
    vfs_unlock();
    return ret;
}

static int ramfs_rename_path(const char *old_abs, const char *old_rel,
                             const char *new_abs, const char *new_rel) {
    (void)old_rel; (void)new_rel;
    vfs_lock();
    struct fs_node *n = resolve_path(old_abs);
    const char *leaf;
    int leaf_len;
    struct fs_node *new_parent = resolve_parent(new_abs, &leaf, &leaf_len);
    if (!n || n == root_node || n == dev_node || n->readonly ||
        !new_parent || new_parent->type != NODE_DIR || leaf_len <= 0 ||
        find_child(new_parent, leaf, leaf_len)) {
        vfs_unlock();
        return -1;
    }
    if (detach_child(n) < 0) {
        vfs_unlock();
        return -1;
    }
    copy_name(n->name, leaf, leaf_len);
    add_child(new_parent, n);
    vfs_unlock();
    return 0;
}

static int ramfs_stat_path(const char *abs, const char *rel, struct stat *st) {
    (void)rel;
    vfs_lock();
    struct fs_node *n = resolve_path(abs);
    if (!n) {
        vfs_unlock();
        return -1;
    }
    st->st_type = node_type_to_dirent(n->type);
    st->st_mode = node_type_to_mode(n->type);
    st->st_size = (uint32_t)n->size;
    vfs_unlock();
    return 0;
}

static int ramfs_is_dir_path(const char *abs, const char *rel) {
    (void)rel;
    vfs_lock();
    struct fs_node *n = resolve_path(abs);
    int ret = (n && n->type == NODE_DIR) ? 1 : 0;
    vfs_unlock();
    return ret;
}

static int ramfs_ls_path(const char *abs, const char *rel, void (*putc)(char)) {
    (void)rel;
    char out[2048];
    int pos = 0;
    vfs_lock();
    struct fs_node *n = resolve_path(abs);
    if (!n) {
        vfs_unlock();
        return -1;
    }
    if (n->type == NODE_DIR) {
        for (struct fs_node *child = n->first_child; child; child = child->next_sibling) {
            const char *s = child->name;
            while (*s && pos < (int)sizeof(out) - 1)
                out[pos++] = *s++;
            if (child->type == NODE_DIR && pos < (int)sizeof(out) - 1)
                out[pos++] = '/';
            if (pos < (int)sizeof(out) - 1)
                out[pos++] = '\n';
        }
    } else {
        const char *s = n->name;
        while (*s && pos < (int)sizeof(out) - 1)
            out[pos++] = *s++;
        if (pos < (int)sizeof(out) - 1)
            out[pos++] = '\n';
    }
    vfs_unlock();

    for (int i = 0; i < pos; i++)
        putc(out[i]);
    return 0;
}

static int minifs_create_path(const char *abs, const char *rel) {
    (void)abs;
    return minifs_create(rel);
}

static int minifs_mkdir_path(const char *abs, const char *rel) {
    (void)abs;
    return minifs_mkdir(rel);
}

static int minifs_unlink_path(const char *abs, const char *rel) {
    (void)abs;
    return minifs_unlink(rel);
}

static int minifs_rmdir_path(const char *abs, const char *rel) {
    (void)abs;
    return minifs_rmdir(rel);
}

static int minifs_rename_path(const char *old_abs, const char *old_rel,
                              const char *new_abs, const char *new_rel) {
    (void)old_abs; (void)new_abs;
    return minifs_rename(old_rel, new_rel);
}

static int minifs_stat_path(const char *abs, const char *rel, struct stat *st) {
    (void)abs;
    return minifs_stat(rel, st);
}

static int minifs_is_dir_path_op(const char *abs, const char *rel) {
    (void)abs;
    return minifs_is_dir_path(rel);
}

static int minifs_ls_path_op(const char *abs, const char *rel, void (*putc)(char)) {
    uint16_t ino;
    struct dirent ents[8];
    size_t off = 0;
    if (minifs_open(rel, &ino) < 0)
        return -1;
    if (minifs_is_dir_ino(ino)) {
        int n;
        while ((n = minifs_getdents(ino, &off, ents, sizeof(ents))) > 0) {
            int count = n / (int)sizeof(struct dirent);
            for (int i = 0; i < count; i++) {
                const char *s = ents[i].d_name;
                while (*s) putc(*s++);
                if (ents[i].d_type == DT_DIR) putc('/');
                putc('\n');
            }
        }
    } else {
        const char *s = abs;
        const char *name = s;
        while (*s) {
            if (*s == '/') name = s + 1;
            s++;
        }
        while (*name) putc(*name++);
        putc('\n');
    }
    return 0;
}

static const struct fs_ops ramfs_ops = {
    .open = ramfs_fs_open,
    .create = ramfs_create_path,
    .mkdir = ramfs_mkdir_path,
    .unlink = ramfs_unlink_path,
    .rmdir = ramfs_rmdir_path,
    .rename = ramfs_rename_path,
    .stat = ramfs_stat_path,
    .is_dir = ramfs_is_dir_path,
    .ls = ramfs_ls_path,
};

static const struct fs_ops minifs_ops = {
    .open = minifs_fs_open,
    .create = minifs_create_path,
    .mkdir = minifs_mkdir_path,
    .unlink = minifs_unlink_path,
    .rmdir = minifs_rmdir_path,
    .rename = minifs_rename_path,
    .stat = minifs_stat_path,
    .is_dir = minifs_is_dir_path_op,
    .ls = minifs_ls_path_op,
};

int vfs_stat(const char *path, struct stat *st) {
    if (!st)
        return -1;
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->stat)
        return -1;
    return mnt->ops->stat(abs, rel, st);
}

static void copy_dirent_name(char *dst, const char *src) {
    int i = 0;
    while (i < FS_NAME_LEN - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    while (++i < FS_NAME_LEN)
        dst[i] = 0;
}

int vfs_getdents(int fd, struct dirent *ents, size_t count) {
    if (!ents)
        return -1;
    size_t max_entries = count / sizeof(struct dirent);
    if (max_entries == 0)
        return -1;

    vfs_lock();
    int owner = current_fd_owner();
    struct open_file *of = fd_to_open_file(owner, fd);
    if (!of || !of->vnode.ops || !of->vnode.ops->getdents) {
        vfs_unlock();
        return -1;
    }
    int ret = of->vnode.ops->getdents(&of->vnode, ents, count);
    vfs_unlock();
    return ret;
}

void ramfs_register(const char *name, const uint8_t *data, size_t size) {
    char abs[128];
    if (normalize_path(name, abs, sizeof(abs)) < 0)
        return;

    vfs_lock();
    const char *leaf;
    int leaf_len;
    struct fs_node *parent = resolve_parent(abs, &leaf, &leaf_len);
    if (parent && parent->type == NODE_DIR && !find_child(parent, leaf, leaf_len)) {
        struct fs_node *n = alloc_node(NODE_FILE, leaf, leaf_len);
        if (n) {
            n->readonly = 1;
            n->ro_data = data;
            n->size = size;
            add_child(parent, n);
        }
    }
    vfs_unlock();
}

int vfs_mkdir(const char *path) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->mkdir)
        return -1;
    return mnt->ops->mkdir(abs, rel);
}

int vfs_create(const char *path) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->create)
        return -1;
    return mnt->ops->create(abs, rel);
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    int fd = vfs_open_flags(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0)
        return -1;
    int ret = vfs_write(fd, data, len);
    vfs_close(fd);
    return ret;
}

int vfs_remove(const char *path) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->unlink)
        return -1;
    return mnt->ops->unlink(abs, rel);
}

int vfs_rmdir(const char *path) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->rmdir)
        return -1;
    return mnt->ops->rmdir(abs, rel);
}

int vfs_rename(const char *old_path, const char *new_path) {
    char old_abs[128];
    char new_abs[128];
    if (normalize_path(old_path, old_abs, sizeof(old_abs)) < 0 ||
        normalize_path(new_path, new_abs, sizeof(new_abs)) < 0)
        return -1;
    const char *old_rel;
    const char *new_rel;
    struct vfs_mount *old_mnt = mount_for_path(old_abs, &old_rel);
    struct vfs_mount *new_mnt = mount_for_path(new_abs, &new_rel);
    if (!old_mnt || old_mnt != new_mnt || !old_mnt->ops->rename)
        return -1;
    return old_mnt->ops->rename(old_abs, old_rel, new_abs, new_rel);
}

int vfs_is_dir(const char *path) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return 0;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->is_dir)
        return 0;
    return mnt->ops->is_dir(abs, rel);
}

int vfs_chdir(const char *path) {
    char abs[128];
    if (normalize_path(path, abs, sizeof(abs)) < 0)
        return -1;
    if (!vfs_is_dir(abs))
        return -1;
    return task_set_cwd(abs);
}

int vfs_getcwd(char *buf, size_t size) {
    return task_get_cwd(buf, (int)size);
}

void vfs_init(void) {
    vfs_lock();
    for (int i = 0; i < FS_MAX_NODES; i++) nodes[i].used = 0;
    for (int i = 0; i < MAX_MOUNTS; i++) mounts[i].used = 0;
    for (int i = 0; i < MAX_PIPES; i++) pipes[i].used = 0;
    for (int t = 0; t < MAX_TASKS; t++) {
        for (int i = 0; i < MAX_FD; i++) {
            fd_used[t][i] = 0;
            fd_open_file[t][i] = -1;
            open_files[t][i].used = 0;
            open_files[t][i].refs = 0;
            open_files[t][i].flags = 0;
            open_files[t][i].stream.pipe_idx = -1;
            open_files[t][i].stream.pipe_end = 0;
        }
    }

    root_node = alloc_node(NODE_DIR, "", 0);
    dev_node = alloc_node(NODE_DIR, "dev", 3);
    fs_node = alloc_node(NODE_DIR, "fs", 2);
    if (root_node && dev_node)
        add_child(root_node, dev_node);
    if (root_node && fs_node)
        add_child(root_node, fs_node);
    add_mount("/", &ramfs_ops);
    add_mount("/dev", &ramfs_ops);
    add_mount("/fs", &minifs_ops);
    vfs_unlock();

    devfs_register("serial",  &serial_dev_ops,  0);
    devfs_register("console", &console_dev_ops, 0);
    devfs_register("null",    &null_dev_ops,    0);
    minifs_mount();

    serial_puts("[vfs] ramfs tree + devfs ready\n");
}

void vfs_task_reset(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS) return;
    vfs_lock();
    for (int i = 0; i < MAX_FD; i++)
        if (fd_used[task_id][i])
            close_fd_locked(task_id, i);
    for (int i = 0; i < MAX_FD; i++) {
        fd_used[task_id][i] = 0;
        fd_open_file[task_id][i] = -1;
        open_files[task_id][i].used = 0;
        open_files[task_id][i].refs = 0;
        open_files[task_id][i].flags = 0;
        open_files[task_id][i].stream.pipe_idx = -1;
        open_files[task_id][i].stream.pipe_end = 0;
    }
    vfs_unlock();
}

int vfs_setup_stdio(int task_id, int console_silent) {
    if (!valid_fd_owner(task_id))
        return -1;
    const char *path = console_silent ? "/dev/null" : "/dev/console";

    vfs_task_reset(task_id);
    vfs_lock();
    int in_fd = open_abs_for_owner(task_id, path, O_RDWR);
    int out_fd = open_abs_for_owner(task_id, path, O_RDWR);
    int err_fd = open_abs_for_owner(task_id, path, O_RDWR);
    vfs_unlock();

    return (in_fd == 0 && out_fd == 1 && err_fd == 2) ? 0 : -1;
}

void vfs_ls_path(const char *path, void (*putc)(char)) {
    char abs[128];

    if (normalize_path((path && path[0]) ? path : ".", abs, sizeof(abs)) < 0)
        return;
    const char *rel;
    struct vfs_mount *mnt = mount_for_path(abs, &rel);
    if (!mnt || !mnt->ops->ls)
        return;
    mnt->ops->ls(abs, rel, putc);
}

void vfs_ls(void (*putc)(char)) {
    vfs_ls_path("/", putc);
}

int vfs_cat(const char *path, void (*putc)(char)) {
    int fd = vfs_open(path);
    if (fd < 0) return -1;
    char buf[128];
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
        for (int i = 0; i < n; i++) putc(buf[i]);
    vfs_close(fd);
    return n < 0 ? -1 : 0;
}
