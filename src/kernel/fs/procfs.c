#include "pmm.h"
#include "task.h"
#include "net.h"
#include "sys_ipc.h"
#include "vfs_internal.h"

enum {
    PROC_NODE_DIR = 0,
    PROC_NODE_TASKS,
    PROC_NODE_THREADS,
    PROC_NODE_MEMINFO,
    PROC_NODE_NET,
    PROC_NODE_SYNC,
    PROC_NODE_FDS,
    PROC_NODE_MOUNTS,
};

struct proc_entry {
    const char *name;
    int node;
};

static const struct proc_entry proc_entries[] = {
    { "tasks", PROC_NODE_TASKS },
    { "threads", PROC_NODE_THREADS },
    { "meminfo", PROC_NODE_MEMINFO },
    { "net", PROC_NODE_NET },
    { "sync", PROC_NODE_SYNC },
    { "fds", PROC_NODE_FDS },
    { "mounts", PROC_NODE_MOUNTS },
};

static int proc_name_len(const char *s) {
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static const char *proc_rel_name(const char *rel) {
    if (!rel)
        return 0;
    while (*rel == '/')
        rel++;
    return rel;
}

static void proc_copy_dirent_name(char *dst, const char *src) {
    int i = 0;
    while (i < FS_NAME_LEN - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    while (++i < FS_NAME_LEN)
        dst[i] = 0;
}

static int proc_find_node(const char *name) {
    int len = proc_name_len(name);
    if (len == 0)
        return PROC_NODE_DIR;
    for (int i = 0; i < (int)(sizeof(proc_entries) / sizeof(proc_entries[0])); i++) {
        if (nameeq(proc_entries[i].name, name, len))
            return proc_entries[i].node;
    }
    return -1;
}

static void append_char(char *buf, int *pos, int cap, char ch) {
    if (*pos < cap - 1)
        buf[*pos] = ch;
    (*pos)++;
}

static void append_text(char *buf, int *pos, int cap, const char *s) {
    while (s && *s)
        append_char(buf, pos, cap, *s++);
}

static void append_u32(char *buf, int *pos, int cap, uint32_t value) {
    char tmp[12];
    int n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0)
        append_char(buf, pos, cap, tmp[--n]);
}

static int proc_meminfo_text(char *buf, int cap) {
    struct pmm_info info;
    int pos = 0;
    pmm_info(&info);
    append_text(buf, &pos, cap, "page_size ");
    append_u32(buf, &pos, cap, info.page_size);
    append_char(buf, &pos, cap, '\n');
    append_text(buf, &pos, cap, "managed_limit ");
    append_u32(buf, &pos, cap, info.managed_limit);
    append_char(buf, &pos, cap, '\n');
    append_text(buf, &pos, cap, "managed_pages ");
    append_u32(buf, &pos, cap, info.managed_pages);
    append_char(buf, &pos, cap, '\n');
    append_text(buf, &pos, cap, "used_pages ");
    append_u32(buf, &pos, cap, info.used_pages);
    append_char(buf, &pos, cap, '\n');
    append_text(buf, &pos, cap, "free_pages ");
    append_u32(buf, &pos, cap, info.free_pages);
    append_char(buf, &pos, cap, '\n');
    if (pos > cap - 1)
        pos = cap - 1;
    buf[pos] = 0;
    return pos;
}

static int proc_mounts_text(char *buf, int cap) {
    int pos = 0;
    append_text(buf, &pos, cap, "/ ramfs\n");
    append_text(buf, &pos, cap, "/dev devfs\n");
    append_text(buf, &pos, cap, "/proc procfs\n");
    append_text(buf, &pos, cap, "/fs minifs\n");
    if (pos > cap - 1)
        pos = cap - 1;
    buf[pos] = 0;
    return pos;
}

static const char *fd_kind(struct open_file *of) {
    if (!of || !of->used)
        return "bad";
    if (of->stream.pipe_idx >= 0)
        return "pipe";
    if (!of->vnode.name)
        return "anon";
    if (nameeq(of->vnode.name, "console", 7) || nameeq(of->vnode.name, "null", 4) ||
        nameeq(of->vnode.name, "serial", 6))
        return "dev";
    if (nameeq(of->vnode.name, "proc", 4))
        return "proc";
    return "file";
}

static void append_flags(char *buf, int *pos, int cap, int flags) {
    int mode = flags & 3;
    if (mode == O_WRONLY)
        append_text(buf, pos, cap, "w");
    else if (mode == O_RDWR)
        append_text(buf, pos, cap, "rw");
    else
        append_text(buf, pos, cap, "r");
    if (flags & O_APPEND)
        append_text(buf, pos, cap, "+a");
    if (flags & O_TRUNC)
        append_text(buf, pos, cap, "+t");
    if (flags & O_CREAT)
        append_text(buf, pos, cap, "+c");
}

static int proc_fds_text(char *buf, int cap) {
    int pos = 0;
    int owner = current_fd_owner();
    append_text(buf, &pos, cap, "OWNER FD OF REFS FLAGS KIND NAME DETAIL\n");
    if (!valid_fd_owner(owner)) {
        append_text(buf, &pos, cap, "invalid\n");
    } else {
        for (int fd = 0; fd < MAX_FD; fd++) {
            if (!fd_used[owner][fd])
                continue;
            int of_idx = fd_open_file[owner][fd];
            struct open_file *of = fd_to_open_file(owner, fd);
            append_u32(buf, &pos, cap, (uint32_t)owner);
            append_char(buf, &pos, cap, ' ');
            append_u32(buf, &pos, cap, (uint32_t)fd);
            append_char(buf, &pos, cap, ' ');
            append_u32(buf, &pos, cap, (uint32_t)of_idx);
            append_char(buf, &pos, cap, ' ');
            append_u32(buf, &pos, cap, of ? (uint32_t)of->refs : 0);
            append_char(buf, &pos, cap, ' ');
            if (of)
                append_flags(buf, &pos, cap, of->flags);
            else
                append_text(buf, &pos, cap, "-");
            append_char(buf, &pos, cap, ' ');
            append_text(buf, &pos, cap, fd_kind(of));
            append_char(buf, &pos, cap, ' ');
            append_text(buf, &pos, cap, (of && of->vnode.name) ? of->vnode.name : "-");
            append_char(buf, &pos, cap, ' ');
            if (of && of->stream.pipe_idx >= 0) {
                struct pipe_obj *p = &pipes[of->stream.pipe_idx];
                append_text(buf, &pos, cap, of->stream.pipe_end == PIPE_READ_END ? "read pipe=" : "write pipe=");
                append_u32(buf, &pos, cap, (uint32_t)of->stream.pipe_idx);
                append_text(buf, &pos, cap, " used=");
                append_u32(buf, &pos, cap, (uint32_t)p->used);
                append_text(buf, &pos, cap, " bytes=");
                append_u32(buf, &pos, cap, (uint32_t)p->count);
                append_text(buf, &pos, cap, " readers=");
                append_u32(buf, &pos, cap, (uint32_t)p->readers);
                append_text(buf, &pos, cap, " writers=");
                append_u32(buf, &pos, cap, (uint32_t)p->writers);
            } else if (of) {
                append_text(buf, &pos, cap, "pos=");
                append_u32(buf, &pos, cap, (uint32_t)of->stream.pos);
            } else {
                append_text(buf, &pos, cap, "-");
            }
            append_char(buf, &pos, cap, '\n');
        }
    }
    if (pos > cap - 1)
        pos = cap - 1;
    buf[pos] = 0;
    return pos;
}

static int proc_build_text(int node, char *buf, int cap) {
    if (node == PROC_NODE_TASKS)
        return task_dump_text(buf, cap, 0);
    if (node == PROC_NODE_THREADS)
        return task_dump_threads_text(buf, cap, 0);
    if (node == PROC_NODE_MEMINFO)
        return proc_meminfo_text(buf, cap);
    if (node == PROC_NODE_NET)
        return net_status_text(buf, cap);
    if (node == PROC_NODE_SYNC)
        return futex_status_text(buf, cap);
    if (node == PROC_NODE_FDS)
        return proc_fds_text(buf, cap);
    if (node == PROC_NODE_MOUNTS)
        return proc_mounts_text(buf, cap);
    return -1;
}

static int proc_open_vn(vnode_t *vn) { (void)vn; return 0; }
static int proc_close_vn(vnode_t *vn) { (void)vn; return 0; }

static int proc_file_read(vnode_t *vn, void *buf, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    char tmp[2048];
    int node;
    int len;
    if (!s || !buf)
        return -1;
    node = (int)s->minifs_ino;
    len = proc_build_text(node, tmp, sizeof(tmp));
    if (len < 0)
        return -1;
    if (s->pos >= (size_t)len)
        return 0;
    size_t avail = (size_t)len - s->pos;
    if (count > avail)
        count = avail;
    for (size_t i = 0; i < count; i++)
        ((uint8_t *)buf)[i] = (uint8_t)tmp[s->pos + i];
    s->pos += count;
    return (int)count;
}

static int proc_file_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}

static int proc_file_size(vnode_t *vn, size_t *size_out) {
    struct file_stream *s = (struct file_stream *)vn->data;
    char tmp[2048];
    int len;
    if (!s || !size_out)
        return -1;
    len = proc_build_text((int)s->minifs_ino, tmp, sizeof(tmp));
    if (len < 0)
        return -1;
    *size_out = (size_t)len;
    return 0;
}

static int proc_dir_read(vnode_t *vn, void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}

static int proc_dir_write(vnode_t *vn, const void *buf, size_t count) {
    (void)vn; (void)buf; (void)count;
    return -1;
}

static int proc_dir_getdents(vnode_t *vn, struct dirent *ents, size_t count) {
    struct file_stream *s = (struct file_stream *)vn->data;
    size_t max_entries = count / sizeof(struct dirent);
    size_t copied = 0;
    if (!s || max_entries == 0)
        return -1;
    for (int i = (int)s->pos;
         i < (int)(sizeof(proc_entries) / sizeof(proc_entries[0])) && copied < max_entries;
         i++) {
        ents[copied].d_type = DT_REG;
        ents[copied].d_size = 0;
        proc_copy_dirent_name(ents[copied].d_name, proc_entries[i].name);
        copied++;
        s->pos++;
    }
    return (int)(copied * sizeof(struct dirent));
}

static const struct vnode_ops proc_file_ops = {
    .open = proc_open_vn,
    .read = proc_file_read,
    .write = proc_file_write,
    .size = proc_file_size,
    .close = proc_close_vn,
};

static const struct vnode_ops proc_dir_ops = {
    .open = proc_open_vn,
    .read = proc_dir_read,
    .write = proc_dir_write,
    .getdents = proc_dir_getdents,
    .close = proc_close_vn,
};

static int procfs_open_path(const char *abs, const char *rel, int flags, struct open_file *of) {
    (void)abs;
    const char *name = proc_rel_name(rel);
    int node = proc_find_node(name);
    if (node < 0)
        return -1;
    if (open_can_write(flags))
        return -1;

    init_open_file(of, flags);
    of->vnode.name = node == PROC_NODE_DIR ? "proc" : name;
    of->vnode.ops = node == PROC_NODE_DIR ? &proc_dir_ops : &proc_file_ops;
    of->stream.minifs_ino = (uint16_t)node;
    of->vnode.data = &of->stream;
    return 0;
}

static int procfs_stat_path(const char *abs, const char *rel, struct stat *st) {
    (void)abs;
    const char *name = proc_rel_name(rel);
    int node = proc_find_node(name);
    if (node < 0 || !st)
        return -1;
    if (node == PROC_NODE_DIR) {
        st->st_type = DT_DIR;
        st->st_mode = S_IFDIR | 0555u;
        st->st_size = 0;
        return 0;
    }
    char tmp[2048];
    int len = proc_build_text(node, tmp, sizeof(tmp));
    st->st_type = DT_REG;
    st->st_mode = S_IFREG | 0444u;
    st->st_size = len < 0 ? 0 : (uint32_t)len;
    return 0;
}

static int procfs_is_dir_path(const char *abs, const char *rel) {
    (void)abs;
    const char *name = proc_rel_name(rel);
    return (name && !name[0]) ? 1 : 0;
}

static int procfs_ls_path(const char *abs, const char *rel, void (*putc)(char)) {
    (void)abs;
    const char *name = proc_rel_name(rel);
    int node = proc_find_node(name);
    if (node < 0)
        return -1;
    if (node != PROC_NODE_DIR) {
        while (*name)
            putc(*name++);
        putc('\n');
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(proc_entries) / sizeof(proc_entries[0])); i++) {
        const char *s = proc_entries[i].name;
        while (*s)
            putc(*s++);
        putc('\n');
    }
    return 0;
}

static const struct fs_ops procfs_ops = {
    .open = procfs_open_path,
    .stat = procfs_stat_path,
    .is_dir = procfs_is_dir_path,
    .ls = procfs_ls_path,
};

void procfs_init(void) {
    vfs_mount("/proc", &procfs_ops);
}
