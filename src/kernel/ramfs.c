#include "ramfs.h"

#define MAX_FILES 16

struct ramfs_file {
    const char    *name;
    const uint8_t *data;
    size_t         size;
};

static struct ramfs_file files[MAX_FILES];
static int file_count;

void ramfs_register(const char *name, const uint8_t *data, size_t size) {
    if (file_count >= MAX_FILES) return;
    files[file_count].name = name;
    files[file_count].data = data;
    files[file_count].size = size;
    file_count++;
}

const uint8_t *ramfs_find(const char *name, size_t *out_size) {
    for (int i = 0; i < file_count; i++) {
        const char *a = name;
        const char *b = files[i].name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            if (out_size) *out_size = files[i].size;
            return files[i].data;
        }
    }
    return 0;
}

void ramfs_init(void) {
    file_count = 0;
}
