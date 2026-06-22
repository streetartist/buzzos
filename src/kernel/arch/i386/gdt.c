#include "gdt.h"
#include "serial.h"

enum {
    GDT_ENTRY_COUNT = 6,
    GDT_LIMIT_4G = 0xFFFFF,
    GDT_ACCESS_KCODE = 0x9A,
    GDT_ACCESS_KDATA = 0x92,
    GDT_ACCESS_UCODE = 0xFA,
    GDT_ACCESS_UDATA = 0xF2,
    GDT_ACCESS_TSS = 0x89,
    GDT_GRANULARITY_4K_32BIT = 0xCF,
};

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_ptr gdt_ptr;

__attribute__((aligned(16), used))
tss32_t tss;

static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt[index].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[index].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[index].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].access      = access;
    gdt[index].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (granularity & 0xF0));
    gdt[index].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

static void load_kernel_segments(void) {
    __asm__ volatile(
        "movw %0, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        :
        : "i"(GDT_SEL_KDATA32)
        : "ax", "memory");
}

static void init_tss(void) {
    tss.ss0 = GDT_SEL_KDATA32;
    tss.esp0 = 0x90000;
    tss.iomap_base = sizeof(tss32_t);
}

void gdt_install(void) {
    uint32_t base = (uint32_t)&tss;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, GDT_LIMIT_4G, GDT_ACCESS_KCODE, GDT_GRANULARITY_4K_32BIT);
    gdt_set_entry(2, 0, GDT_LIMIT_4G, GDT_ACCESS_KDATA, GDT_GRANULARITY_4K_32BIT);
    gdt_set_entry(3, 0, GDT_LIMIT_4G, GDT_ACCESS_UCODE, GDT_GRANULARITY_4K_32BIT);
    gdt_set_entry(4, 0, GDT_LIMIT_4G, GDT_ACCESS_UDATA, GDT_GRANULARITY_4K_32BIT);
    gdt_set_entry(5, base, sizeof(tss32_t) - 1, GDT_ACCESS_TSS, 0x00);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt;
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr) : "memory");

    load_kernel_segments();
    init_tss();

    /* Load Task Register */
    __asm__ volatile("ltr %%ax" : : "a"(GDT_SEL_TSS));

    serial_puts("[boot] gdt + tss ok\n");
}
