#ifndef BUZZOS_VGA_H
#define BUZZOS_VGA_H

#include <stdint.h>

enum {
    VGA_WIDTH  = 80,
    VGA_HEIGHT = 25,
    VGA_ADDR   = 0xB8000,
    VGA_GFX_WIDTH = 320,
    VGA_GFX_HEIGHT = 200,
};

void vga_init(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_backspace(void);
void vga_update_cursor(void);
int  vga_set_mode(int mode);
int  vga_gfx_clear(uint8_t color);
int  vga_gfx_putpixel(int x, int y, uint8_t color);
int  vga_gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
int  vga_gfx_blit(int x, int y, int w, int h, const uint8_t *pixels);
int  vga_gfx_text(int x, int y, const char *s, uint8_t fg, int bg);

#endif /* BUZZOS_VGA_H */
