#ifndef BUZZOS_VGA_H
#define BUZZOS_VGA_H

#include <stdint.h>

enum {
    VGA_WIDTH  = 80,
    VGA_HEIGHT = 25,
    VGA_ADDR   = 0xB8000,
};

void vga_init(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_backspace(void);
void vga_update_cursor(void);

#endif /* BUZZOS_VGA_H */
