#ifndef BUZZOS_RAMFS_H
#define BUZZOS_RAMFS_H

#include <stddef.h>
#include <stdint.h>

/* Minimal in-memory filesystem. Register files at boot, look them up
 * by name at runtime. Only supports read-only access to whole files
 * — no directories, no write, no partial read. */

/* Register a file. Name is copied, data is referenced (not copied). */
void ramfs_register(const char *name, const uint8_t *data, size_t size);

/* Find a file by name. Returns 0 if not found. */
const uint8_t *ramfs_find(const char *name, size_t *out_size);

/* Boot-time initialisation (currently a no-op). */
void ramfs_init(void);

#endif /* BUZZOS_RAMFS_H */
