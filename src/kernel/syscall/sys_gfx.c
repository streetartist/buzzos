#include "syscall_internal.h"
#include "mouse.h"
#include "vga.h"

int sys_gfx_mode(uint32_t mode, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vga_set_mode((int)mode);
}

int sys_gfx_clear(uint32_t color, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vga_gfx_clear((uint8_t)color);
}

int sys_gfx_putpixel(uint32_t x, uint32_t y, uint32_t color, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return vga_gfx_putpixel((int)x, (int)y, (uint8_t)color);
}

int sys_gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    return vga_gfx_fill_rect((int)x, (int)y, (int)w, (int)h, (uint8_t)color);
}

int sys_gfx_text(uint32_t x, uint32_t y, uint32_t s_arg, uint32_t fg, uint32_t bg) {
    const char *s = (const char *)(uintptr_t)s_arg;
    if (!user_string_ok(s))
        return -1;
    return vga_gfx_text((int)x, (int)y, s, (uint8_t)fg, (int)bg);
}

int sys_fb_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t pixels_arg) {
    if (w == 0 || h == 0)
        return 0;
    if (x >= VGA_GFX_WIDTH || y >= VGA_GFX_HEIGHT)
        return -1;
    if (w > VGA_GFX_WIDTH || h > VGA_GFX_HEIGHT)
        return -1;
    if (x + w > VGA_GFX_WIDTH || y + h > VGA_GFX_HEIGHT)
        return -1;
    if (!user_range_ok(pixels_arg, w * h))
        return -1;
    const uint8_t *pixels = (const uint8_t *)(uintptr_t)pixels_arg;
    return vga_gfx_blit((int)x, (int)y, (int)w, (int)h, pixels);
}

int sys_mouse_get(uint32_t out_arg, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!user_range_ok(out_arg, sizeof(struct mouse_state)))
        return -1;
    struct mouse_state *out = (struct mouse_state *)(uintptr_t)out_arg;
    mouse_get_state(out);
    return 0;
}
