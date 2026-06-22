#ifndef BUZZOS_GDT_H
#define BUZZOS_GDT_H
#include <stdint.h>

struct tss32 {
    uint16_t link, _link_h;
    uint32_t esp0;  uint16_t ss0, _ss0_h;
    uint32_t esp1;  uint16_t ss1, _ss1_h;
    uint32_t esp2;  uint16_t ss2, _ss2_h;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es, _es_h, cs, _cs_h, ss, _ss_h, ds, _ds_h, fs, _fs_h, gs, _gs_h;
    uint16_t ldt, _ldt_h, trap, iomap_base;
} __attribute__((packed));
typedef struct tss32 tss32_t;

/* Selectors matching boot-sector GDT layout */
#define GDT_SEL_KCODE32  0x08
#define GDT_SEL_KDATA32  0x10
#define GDT_SEL_UCODE32  0x1B
#define GDT_SEL_UDATA32  0x23
#define GDT_SEL_TSS      0x28

extern tss32_t tss;
void gdt_install(void);
#endif
