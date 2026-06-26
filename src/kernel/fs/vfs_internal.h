#ifndef BUZZOS_VFS_INTERNAL_H
#define BUZZOS_VFS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "task.h"
#include "vfs.h"

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

struct fs_node;

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
    uint32_t read_waiters;
    uint32_t write_waiters;
    uint8_t data[PIPE_BUFSZ];
};

extern struct open_file open_files[MAX_TASKS][MAX_FD];
extern int fd_open_file[MAX_TASKS][MAX_FD];
extern int fd_used[MAX_TASKS][MAX_FD];
extern struct pipe_obj pipes[MAX_PIPES];

void ramfs_init(void);
void devfs_init(void);
void pipefs_init(void);
void minifs_vfs_init(void);
void procfs_init(void);

int vfs_mount(const char *path, const struct fs_ops *ops);
void vfs_lock(void);
void vfs_unlock(void);
int nameeq(const char *name, const char *part, int len);
void copy_name(char *dst, const char *src, int len);
int current_fd_owner(void);
int valid_fd_owner(int owner);
int vfs_normalize_path(const char *in, char *out, int out_sz);
int alloc_fd_slot(int owner);
int alloc_open_file(int owner);
struct open_file *fd_to_open_file(int owner, int fd);
int open_can_read(int flags);
int open_can_write(int flags);
void init_open_file(struct open_file *of, int flags);

#endif
