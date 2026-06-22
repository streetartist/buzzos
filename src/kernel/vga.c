#include <stddef.h>
#include "io.h"
#include "vga.h"

static volatile uint16_t *const vga = (uint16_t *)VGA_ADDR;
static uint8_t row, col;
static uint8_t color = 0x0F;

static uint16_t vga_cell(char ch) {
    return (uint16_t)(uint8_t)ch | ((uint16_t)color << 8);
}

void vga_init(void) { row = 0; col = 0; vga_clear(); }

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

static void newline(void) {
    col = 0;
    if (++row < VGA_HEIGHT) return;
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            vga[(y-1)*VGA_WIDTH + x] = vga[y*VGA_WIDTH + x];
    row = (uint8_t)(VGA_HEIGHT - 1);
    for (size_t x = 0; x < VGA_WIDTH; x++)
        vga[row*VGA_WIDTH + x] = vga_cell(' ');
}

void vga_clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = vga_cell(' ');
    row = 0; col = 0;
}

void vga_putc(char c) {
    if (c == '\n') { newline(); vga_update_cursor(); return; }
    vga[row * VGA_WIDTH + col] = vga_cell(c);
    if (++col == VGA_WIDTH) newline();
    vga_update_cursor();
}

void vga_puts(const char *s) { while (*s) vga_putc(*s++); }

void vga_backspace(void) {
    if (col > 0) col--;
    else if (row > 0) { row--; col = VGA_WIDTH - 1; }
    else return;
    vga[row * VGA_WIDTH + col] = vga_cell(' ');
    vga_update_cursor();
}

void vga_update_cursor(void) {
    uint16_t pos = row * VGA_WIDTH + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
