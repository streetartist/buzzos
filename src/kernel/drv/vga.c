#include <stddef.h>
#include "io.h"
#include "vga.h"

static volatile uint16_t *const vga = (uint16_t *)VGA_ADDR;
static volatile uint8_t *const gfx = (uint8_t *)0xA0000;
static uint8_t row, col;
static uint8_t color = 0x0F;
static uint8_t ansi_state;
static int ansi_params[2];
static int ansi_param_index;
static int ansi_seen_digit;
static int current_mode;
static uint8_t saved_text_font[256 * 32];
static int saved_text_font_valid;

enum {
    VGA_MODE_TEXT = 0,
    VGA_MODE_13H = 1,
};

static const uint8_t regs_text_80x25[] = {
    0x67,
    0x03, 0x00, 0x03, 0x00, 0x02,
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00,
};

static const uint8_t regs_320x200x256[] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00,
};

static const uint8_t font_digits[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
};

static const uint8_t font_upper[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static void ansi_reset(void);
static void vga_load_builtin_font(void);
static void vga_save_text_font(void);

static uint16_t vga_cell(char ch) {
    return (uint16_t)(uint8_t)ch | ((uint16_t)color << 8);
}

void vga_init(void) {
    row = 0;
    col = 0;
    current_mode = VGA_MODE_TEXT;
    ansi_reset();
    vga_save_text_font();
    vga_clear();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

static void vga_write_regs(const uint8_t *regs) {
    outb(0x3C2, *regs++);

    for (uint8_t i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, *regs++);
    }

    outb(0x3D4, 0x03);
    outb(0x3D5, (uint8_t)(inb(0x3D5) | 0x80));
    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(inb(0x3D5) & ~0x80));

    for (uint8_t i = 0; i < 25; i++) {
        uint8_t v = *regs++;
        if (i == 0x03)
            v |= 0x80;
        if (i == 0x11)
            v &= (uint8_t)~0x80;
        outb(0x3D4, i);
        outb(0x3D5, v);
    }

    for (uint8_t i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, *regs++);
    }

    for (uint8_t i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, *regs++);
    }
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

static void set_dac(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, index);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
}

static void set_basic_palette(void) {
    static const uint8_t pal[16][3] = {
        {0,0,0}, {0,0,42}, {0,42,0}, {0,42,42},
        {42,0,0}, {42,0,42}, {42,21,0}, {42,42,42},
        {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
        {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},
    };
    for (uint8_t i = 0; i < 16; i++)
        set_dac(i, pal[i][0], pal[i][1], pal[i][2]);
    set_dac(16, 6, 10, 18);
    set_dac(17, 10, 18, 32);
    set_dac(18, 14, 26, 44);
    set_dac(19, 20, 34, 54);
    set_dac(20, 28, 44, 62);
    set_dac(21, 8, 30, 24);
    set_dac(22, 18, 46, 38);
    set_dac(23, 50, 40, 16);
    set_dac(24, 54, 22, 20);
}

static void disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

int vga_set_mode(int mode) {
    if (mode == VGA_MODE_13H) {
        vga_write_regs(regs_320x200x256);
        set_basic_palette();
        current_mode = VGA_MODE_13H;
        disable_cursor();
        return vga_gfx_clear(16);
    }
    if (mode == VGA_MODE_TEXT) {
        vga_write_regs(regs_text_80x25);
        set_basic_palette();
        vga_load_builtin_font();
        current_mode = VGA_MODE_TEXT;
        ansi_reset();
        vga_clear();
        return 0;
    }
    return -1;
}

int vga_gfx_putpixel(int x, int y, uint8_t color_index) {
    if (current_mode != VGA_MODE_13H)
        return -1;
    if (x < 0 || y < 0 || x >= VGA_GFX_WIDTH || y >= VGA_GFX_HEIGHT)
        return -1;
    gfx[y * VGA_GFX_WIDTH + x] = color_index;
    return 0;
}

int vga_gfx_clear(uint8_t color_index) {
    if (current_mode != VGA_MODE_13H)
        return -1;
    for (int i = 0; i < VGA_GFX_WIDTH * VGA_GFX_HEIGHT; i++)
        gfx[i] = color_index;
    return 0;
}

int vga_gfx_fill_rect(int x, int y, int w, int h, uint8_t color_index) {
    if (current_mode != VGA_MODE_13H)
        return -1;
    if (w <= 0 || h <= 0)
        return 0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= VGA_GFX_WIDTH || y >= VGA_GFX_HEIGHT)
        return 0;
    if (x + w > VGA_GFX_WIDTH)
        w = VGA_GFX_WIDTH - x;
    if (y + h > VGA_GFX_HEIGHT)
        h = VGA_GFX_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return 0;
    for (int yy = 0; yy < h; yy++) {
        volatile uint8_t *rowp = gfx + (y + yy) * VGA_GFX_WIDTH + x;
        for (int xx = 0; xx < w; xx++)
            rowp[xx] = color_index;
    }
    return 0;
}

int vga_gfx_blit(int x, int y, int w, int h, const uint8_t *pixels) {
    if (current_mode != VGA_MODE_13H || !pixels)
        return -1;
    if (x < 0 || y < 0 || w <= 0 || h <= 0)
        return -1;
    if (x >= VGA_GFX_WIDTH || y >= VGA_GFX_HEIGHT)
        return -1;
    if (x + w > VGA_GFX_WIDTH || y + h > VGA_GFX_HEIGHT)
        return -1;

    for (int yy = 0; yy < h; yy++) {
        volatile uint8_t *dst = gfx + (y + yy) * VGA_GFX_WIDTH + x;
        const uint8_t *src = pixels + yy * w;
        for (int xx = 0; xx < w; xx++)
            dst[xx] = src[xx];
    }
    return 0;
}

static const uint8_t *glyph_for(char c) {
    static const uint8_t space[7] = {0,0,0,0,0,0,0};
    static const uint8_t dash[7]  = {0,0,0,0x1F,0,0,0};
    static const uint8_t dot[7]   = {0,0,0,0,0,0x0C,0x0C};
    static const uint8_t colon[7] = {0,0x0C,0x0C,0,0x0C,0x0C,0};
    static const uint8_t semi[7]  = {0,0x0C,0x0C,0,0x0C,0x0C,0x08};
    static const uint8_t comma[7] = {0,0,0,0,0x0C,0x0C,0x08};
    static const uint8_t slash[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    static const uint8_t bslash[7]= {0x10,0x08,0x08,0x04,0x02,0x02,0x01};
    static const uint8_t bang[7]  = {0x04,0x04,0x04,0x04,0x04,0,0x04};
    static const uint8_t ques[7]  = {0x0E,0x11,0x01,0x02,0x04,0,0x04};
    static const uint8_t gt[7]    = {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
    static const uint8_t lt[7]    = {0x01,0x02,0x04,0x08,0x04,0x02,0x01};
    static const uint8_t plus[7]  = {0,0x04,0x04,0x1F,0x04,0x04,0};
    static const uint8_t eq[7]    = {0,0,0x1F,0,0x1F,0,0};
    static const uint8_t under[7] = {0,0,0,0,0,0,0x1F};
    static const uint8_t lpar[7]  = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    static const uint8_t rpar[7]  = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    static const uint8_t lbr[7]   = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E};
    static const uint8_t rbr[7]   = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
    static const uint8_t quote[7] = {0x0A,0x0A,0,0,0,0,0};
    static const uint8_t apos[7]  = {0x04,0x04,0,0,0,0,0};
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return font_upper[c - 'A'];
    if (c >= '0' && c <= '9')
        return font_digits[c - '0'];
    if (c == '-') return dash;
    if (c == '.') return dot;
    if (c == ':') return colon;
    if (c == ';') return semi;
    if (c == ',') return comma;
    if (c == '/') return slash;
    if (c == '\\') return bslash;
    if (c == '!') return bang;
    if (c == '?') return ques;
    if (c == '>') return gt;
    if (c == '<') return lt;
    if (c == '+') return plus;
    if (c == '=') return eq;
    if (c == '_') return under;
    if (c == '(') return lpar;
    if (c == ')') return rpar;
    if (c == '[') return lbr;
    if (c == ']') return rbr;
    if (c == '"') return quote;
    if (c == '\'') return apos;
    return space;
}

static uint8_t font_expand_row(uint8_t bits) {
    uint8_t out = 0;
    for (int x = 0; x < 5; x++) {
        if (bits & (1 << (4 - x)))
            out |= (uint8_t)(1 << (6 - x));
    }
    return out;
}

static void font_write_char(volatile uint8_t *font_mem, int ch) {
    const uint8_t *g = glyph_for((char)ch);
    volatile uint8_t *dst = font_mem + ch * 32;
    for (int i = 0; i < 32; i++)
        dst[i] = 0;
    for (int r = 0; r < 7; r++) {
        uint8_t v = font_expand_row(g[r]);
        dst[1 + r * 2] = v;
        dst[2 + r * 2] = v;
    }
}

static void vga_select_font_plane(void) {
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x04);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x07);
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x02);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x00);
}

static void vga_select_text_plane(void) {
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x03);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x03);
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x10);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x0E);
}

static void vga_save_text_font(void) {
    volatile uint8_t *font_mem = (volatile uint8_t *)0xA0000;
    vga_select_font_plane();
    for (int i = 0; i < (int)sizeof(saved_text_font); i++)
        saved_text_font[i] = font_mem[i];
    saved_text_font_valid = 1;
    vga_select_text_plane();
}

static void vga_load_builtin_font(void) {
    volatile uint8_t *font_mem = (volatile uint8_t *)0xA0000;
    vga_select_font_plane();
    if (saved_text_font_valid) {
        for (int i = 0; i < (int)sizeof(saved_text_font); i++)
            font_mem[i] = saved_text_font[i];
    } else {
        for (int ch = 0; ch < 256; ch++)
            font_write_char(font_mem, ch);
    }
    vga_select_text_plane();
}

static void gfx_char(int x, int y, char c, uint8_t fg, int bg) {
    const uint8_t *g = glyph_for(c);
    for (int yy = 0; yy < 7; yy++) {
        for (int xx = 0; xx < 5; xx++) {
            int on = (g[yy] & (1 << (4 - xx))) != 0;
            if (on)
                vga_gfx_putpixel(x + xx, y + yy, fg);
            else if (bg >= 0)
                vga_gfx_putpixel(x + xx, y + yy, (uint8_t)bg);
        }
    }
}

int vga_gfx_text(int x, int y, const char *s, uint8_t fg, int bg) {
    if (current_mode != VGA_MODE_13H || !s)
        return -1;
    int start_x = x;
    while (*s) {
        if (*s == '\n') {
            x = start_x;
            y += 9;
            s++;
            continue;
        }
        gfx_char(x, y, *s++, fg, bg);
        x += 6;
    }
    return 0;
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
