#include "pmm.h"
#include "serial.h"

/* ------------------------------------------------------------------ */
/*  Bitmap (1 bit per 4 KiB page, up to 256 MiB)                       */
/* ------------------------------------------------------------------ */

#define MAX_PAGES  (65536)   /* 256 MiB ÷ 4 KiB */
#define BITMAP_WORDS (MAX_PAGES / 32)

static uint32_t bitmap[BITMAP_WORDS];
static size_t   total_pages;

static void bit_set(size_t idx)   { bitmap[idx / 32] |=  (1u << (idx % 32)); }
static void bit_clr(size_t idx)   { bitmap[idx / 32] &= ~(1u << (idx % 32)); }
static int  bit_get(size_t idx)   { return (bitmap[idx / 32] >> (idx % 32)) & 1; }

/* ------------------------------------------------------------------ */
/*  Mark a physical range as used or free                              */
/* ------------------------------------------------------------------ */

static void mark_range(uintptr_t base, size_t bytes, int used) {
    size_t page = (size_t)(base / PAGE_SIZE);
    size_t count = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < count && page + i < MAX_PAGES; i++) {
        if (used) bit_set(page + i); else bit_clr(page + i);
    }
}

/* ------------------------------------------------------------------ */
/*  Initialise                                                         */
/* ------------------------------------------------------------------ */

extern uint8_t __bss_end;   /* first free byte after kernel BSS */

void pmm_init(void) {
    serial_puts("[pmm] scanning E820 map...\n");

    /* Default: all pages used */
    for (size_t i = 0; i < BITMAP_WORDS; i++) bitmap[i] = ~0u;

    uint16_t entries = E820_COUNT;
    total_pages = 0;

    serial_puts("[pmm] entries: ");
    serial_puthex(entries);
    serial_puts("\n");

    /* Mark usable regions as free */
    for (uint16_t i = 0; i < entries && i < 128; i++) {
        if (E820_BUF[i].type == E820_USABLE) {
            uint64_t base  = E820_BUF[i].base;
            uint64_t len   = E820_BUF[i].length;
            /* Only track memory below 256 MiB */
            if (base >= MAX_PAGES * PAGE_SIZE) continue;
            if (base + len > MAX_PAGES * PAGE_SIZE)
                len = (uint64_t)MAX_PAGES * PAGE_SIZE - base;
            size_t pages = (size_t)(len / PAGE_SIZE);
            total_pages += pages;
            mark_range((uintptr_t)base, (size_t)len, 0);
        }
    }

    /* Reserve kernel area (0x1000 .. __bss_end + bitmap) */
    uintptr_t kernel_start = 0x1000;
    uintptr_t kernel_end   = (uintptr_t)&__bss_end + sizeof(bitmap);
    mark_range(kernel_start, kernel_end - kernel_start, 1);

    /* boot.asm enters the kernel on a stack ending at 0x90000. Task 0 keeps
     * using that stack, so PMM must never hand these pages to user mappings or
     * later kernel stacks. */
    mark_range(0x00080000, 0x00010000, 1);

    /* User processes use 0x001C0000..0x00230000 as private virtual memory.
     * Do not allocate physical kernel objects there, or those virtual
     * addresses will alias user pages after a CR3 switch. */
    mark_range(0x001C0000, 0x00230000 - 0x001C0000, 1);

    /* Reserve first 4 KiB (IVT / BDA) */
    mark_range(0, 0x1000, 1);

    serial_puts("[pmm] total free pages: ");
    serial_puthex((uint32_t)total_pages);
    serial_puts("\n");
}

/* ------------------------------------------------------------------ */
/*  Allocate `n` contiguous pages                                      */
/* ------------------------------------------------------------------ */

uintptr_t pmm_alloc_pages(size_t n) {
    if (n == 0) return 0;

    for (size_t start = 0; start + n <= MAX_PAGES; start++) {
        int ok = 1;
        for (size_t j = 0; j < n; j++) {
            if (bit_get(start + j)) { ok = 0; break; }
        }
        if (ok) {
            for (size_t j = 0; j < n; j++) bit_set(start + j);
            return (uintptr_t)(start * PAGE_SIZE);
        }
    }
    return 0;  /* no contiguous block found */
}

/* ------------------------------------------------------------------ */
/*  Free                                                               */
/* ------------------------------------------------------------------ */

void pmm_free_pages(uintptr_t addr, size_t n) {
    mark_range(addr, n * PAGE_SIZE, 0);
}

/* ------------------------------------------------------------------ */
/*  Debug dump                                                         */
/* ------------------------------------------------------------------ */

void pmm_dump(void) {
    serial_puts("\n--- E820 Memory Map ---\n");
    uint16_t n = E820_COUNT;
    for (uint16_t i = 0; i < n && i < 128; i++) {
        serial_puts("  base=0x");  serial_puthex((uint32_t)E820_BUF[i].base);
        serial_puts(" len=0x");   serial_puthex((uint32_t)E820_BUF[i].length);
        serial_puts(" type=");    serial_puthex(E820_BUF[i].type);
        serial_puts("\n");
    }
    serial_puts("--- end map ---\n");
}
