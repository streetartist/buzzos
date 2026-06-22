#ifndef BUZZOS_BLOCK_CACHE_H
#define BUZZOS_BLOCK_CACHE_H

#include <stdint.h>

void block_cache_init(void);
int block_read_sector(uint32_t lba, void *buf);
int block_write_sector(uint32_t lba, const void *buf);
int block_cache_flush(void);

#endif
