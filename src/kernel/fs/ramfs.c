#include "ramfs.h"
#include "vfs_internal.h"

enum node_type {
    NODE_FREE = 0,
    NODE_DIR,
    NODE_FILE,
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
};

static struct fs_node nodes[FS_MAX_NODES];
static struct fs_node *root_node;
static struct fs_node *dev_node;
static struct fs_node *fs_node;

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
    if (!dir || dir->type != NODE_DIR)
        return 0;
    for (struct fs_node *n = dir->first_child; n; n = n->next_sibling) {
        if (nameeq(n->name, name, len))
            return n;
    }
    return 0;
}

static struct fs_node *resolve_path(const char *path) {
    if (!path || path[0] != '/')
        return 0;
    struct fs_node *cur = root_node;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);
        if (len > 0) {
            cur = find_child(cur, start, len);
            if (!cur)
                return 0;
        }
        while (*p == '/') p++;
    }
    return cur;
}

static struct fs_node *resolve_parent(const char *path, const char **leaf, int *leaf_len) {
    if (!path || path[0] != '/')
        return 0;
    const char *last = 0;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        last = p;
        while (*p && *p != '/') p++;
    }
    if (!last)
        return 0;

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
            if (!cur || cur->type != NODE_DIR)
                return 0;
        }
    }
    return cur;
}

static int unlink_child(struct fs_node *n) {
    struct fs_node *parent = n->parent;
    if (!parent)
        return -1;
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
    if (!parent)
        return -1;
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

static uint32_t node_type_to_dirent(enum node_type type) {
    if (type == NODE_DIR) return DT_DIR;
    if (type == NODE_FILE) return DT_REG;
    return DT_UNKNOWN;
}

static uint32_t node_type_to_mode(enum node_type type) {
    if (type == NODE_DIR) return S_IFDIR | 0755u;
    if (type == NODE_FILE) return S_IFREG | 0644u;
    return 0;
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

static int file_size(vnode_t *vn, size_t *size_out) {
    struct file_stream *s = (struct file_stream *)vn->data;
    if (!s || !s->node || s->node->type != NODE_FILE || !size_out)
        return -1;
    *size_out = s->node->size;
    return 0;
}

static const struct vnode_ops file_ops = {
    .open = file_open,
    .read = file_read,
    .write = file_write,
    .size = file_size,
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
    if (n->type == NODE_DIR) {
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
    if (!n || n == root_node || n == dev_node || n == fs_node ||
        n->readonly || n->type != NODE_FILE) {
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
    if (!n || n == root_node || n == dev_node || n == fs_node || n->readonly ||
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
    if (!n || n == root_node || n == dev_node || n == fs_node || n->readonly ||
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

void ramfs_register(const char *name, const uint8_t *data, size_t size) {
    char abs[128];
    if (vfs_normalize_path(name, abs, sizeof(abs)) < 0)
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

const uint8_t *ramfs_find(const char *name, size_t *out_size) {
    char abs[128];
    if (vfs_normalize_path(name, abs, sizeof(abs)) < 0)
        return 0;
    vfs_lock();
    struct fs_node *n = resolve_path(abs);
    const uint8_t *data = 0;
    if (n && n->type == NODE_FILE && n->readonly) {
        data = n->ro_data;
        if (out_size)
            *out_size = n->size;
    }
    vfs_unlock();
    return data;
}

void ramfs_init(void) {
    for (int i = 0; i < FS_MAX_NODES; i++)
        nodes[i].used = 0;

    root_node = alloc_node(NODE_DIR, "", 0);
    dev_node = alloc_node(NODE_DIR, "dev", 3);
    fs_node = alloc_node(NODE_DIR, "fs", 2);
    if (root_node && dev_node)
        add_child(root_node, dev_node);
    if (root_node && fs_node)
        add_child(root_node, fs_node);
    vfs_mount("/", &ramfs_ops);
}
