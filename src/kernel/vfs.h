#ifndef BUZZOS_VFS_H
#define BUZZOS_VFS_H

#include <stddef.h>
#include <stdint.h>

/* Forward decl */
struct vnode;

/* vnode operations — filesystem implementations fill these in */
struct vnode_ops {
    int  (*open)(struct vnode *vn);
    int  (*read)(struct vnode *vn, void *buf, size_t count);
    int  (*write)(struct vnode *vn, const void *buf, size_t count);
    int  (*close)(struct vnode *vn);
};

/* A vnode — one per open file. name is for devfs lookup. */
struct vnode {
    const char        *name;
    const struct vnode_ops *ops;
    void              *data;    /* fs-private */
};

typedef struct vnode vnode_t;

/* Register a device node (devfs). After registration it can be opened
 * by name via vfs_open("/dev/..."). */
void devfs_register(const char *name, const struct vnode_ops *ops,
                    void *data);

/* Open a file by path. Returns fd >= 0, or -1 on error. */
int  vfs_open(const char *path);

/* Read from fd, returns bytes read or -1 on error. */
int  vfs_read(int fd, void *buf, size_t count);

/* Write to fd, returns bytes written or -1 on error. */
int  vfs_write(int fd, const void *buf, size_t count);

/* Close fd. Returns 0 or -1. */
int  vfs_close(int fd);

/* Register a ramfs file. Data is referenced (not copied). */
void ramfs_register(const char *name, const uint8_t *data, size_t size);

/* Initialise VFS and built-in filesystems. */
void vfs_init(void);

void vfs_ls(void (*putc)(char));
int  vfs_cat(const char *path, void (*putc)(char));

#endif /* BUZZOS_VFS_H */
