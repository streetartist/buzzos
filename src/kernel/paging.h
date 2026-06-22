#ifndef BUZZOS_PAGING_H
#define BUZZOS_PAGING_H

#include <stdint.h>

/* Set up a page directory and page tables:
 *   - Identity-map 0x00000000 → 0x00000000 (first 8 MiB)
 *   - Map 0xC0000000 → 0x00000000 (first 8 MiB, higher-half alias)
 * Then enable paging (CR0.PG) and jump-call to flush the TLB.
 * After this call, virtual == physical for the bottom 8 MiB. */
void paging_init(void);

/* Page directory and table entry flags */
enum {
    PAGE_PRESENT  = 0x01,
    PAGE_RW       = 0x02,
    PAGE_USER     = 0x04,
    PAGE_WT       = 0x08,
    PAGE_CD       = 0x10,
    PAGE_LARGE    = 0x80,   /* 4 MiB page (PDE only) */
};

#endif /* BUZZOS_PAGING_H */
