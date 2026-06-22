#ifndef BUZZOS_IDT_H
#define BUZZOS_IDT_H

#include <stdint.h>

/* A single 8-byte interrupt gate descriptor (Intel SDM Vol 3A, §6.14). */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

typedef struct idt_entry idt_entry_t;

/* The 6-byte pointer for `lidt`. */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct idt_ptr idt_ptr_t;

/* Install the IDT and remap the 8259 PIC. After this call:
 *   - Exceptions 0-31 have handlers that print and halt.
 *   - IRQs 0-15 are remapped to interrupts 32-47.
 *   - All IRQs are masked except the keyboard (IRQ1) which has a stub. */
void idt_install(void);

#endif /* BUZZOS_IDT_H */
