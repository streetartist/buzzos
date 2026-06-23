#include <stddef.h>
#include "io.h"
#include "vga.h"

static volatile uint16_t *const vga = (uint16_t *)VGA_ADDR;
static uint8_t row, col;
static uint8_t color = 0x0F;
static uint8_t ansi_state;
static int ansi_params[2];
static int ansi_param_index;
static int ansi_seen_digit;

static void ansi_reset(void);

static uint16_t vga_cell(char ch) {
    return (uint16_t)(uint8_t)ch | ((uint16_t)color << 8);
}

void vga_init(void) { row = 0; col = 0; ansi_reset(); vga_clear(); }

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

static void ansi_reset(void) {
    ansi_state = 0;
    ansi_params[0] = 0;
    ansi_params[1] = 0;
    ansi_param_index = 0;
    ansi_seen_digit = 0;
}

static int ansi_value_or_default(int value, int fallback) {
    return value > 0 ? value : fallback;
}

static void set_cursor(int r, int c) {
    if (r < 0) r = 0;
    if (c < 0) c = 0;
    if (r >= VGA_HEIGHT) r = VGA_HEIGHT - 1;
    if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
    row = (uint8_t)r;
    col = (uint8_t)c;
    vga_update_cursor();
}

static void clear_line_from_cursor(void) {
    for (size_t x = col; x < VGA_WIDTH; x++)
        vga[row * VGA_WIDTH + x] = vga_cell(' ');
    vga_update_cursor();
}

static int vga_handle_ansi(char c) {
    if (ansi_state == 0) {
        if ((unsigned char)c == 0x1B) {
            ansi_state = 1;
            return 1;
        }
        return 0;
    }

    if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_params[0] = 0;
            ansi_params[1] = 0;
            ansi_param_index = 0;
            ansi_seen_digit = 0;
        } else {
            ansi_reset();
        }
        return 1;
    }

    if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            if (ansi_param_index < 2)
                ansi_params[ansi_param_index] =
                    ansi_params[ansi_param_index] * 10 + (c - '0');
            ansi_seen_digit = 1;
            return 1;
        }
        if (c == ';') {
            if (ansi_param_index < 1)
                ansi_param_index++;
            ansi_seen_digit = 0;
            return 1;
        }
        if (c == '?')
            return 1;

        int n = ansi_value_or_default(ansi_params[0], 1);
        switch (c) {
        case 'A': set_cursor((int)row - n, col); break;
        case 'B': set_cursor((int)row + n, col); break;
        case 'C': set_cursor(row, (int)col + n); break;
        case 'D': set_cursor(row, (int)col - n); break;
        case 'H':
        case 'f':
            set_cursor(ansi_value_or_default(ansi_params[0], 1) - 1,
                       ansi_value_or_default(ansi_params[1], 1) - 1);
            break;
        case 'J':
            if (ansi_params[0] == 2 || !ansi_seen_digit)
                vga_clear();
            break;
        case 'K':
            clear_line_from_cursor();
            break;
        default:
            break;
        }
        ansi_reset();
        return 1;
    }
    ansi_reset();
    return 1;
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
    vga_update_cursor();
}

void vga_putc(char c) {
    if (vga_handle_ansi(c)) return;
    if (c == '\n') { newline(); vga_update_cursor(); return; }
    if (c == '\r') { col = 0; vga_update_cursor(); return; }
    if (c == '\b') { vga_backspace(); return; }
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
