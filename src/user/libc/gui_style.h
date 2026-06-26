#ifndef BUZZOS_GUI_STYLE_H
#define BUZZOS_GUI_STYLE_H

#include "libc.h"

enum {
    UI_SW = 320,
    UI_SH = 200,
    UI_TOP_H = 12,
    UI_EXIT_X = 286,
    UI_EXIT_Y = 2,
    UI_EXIT_W = 28,
    UI_EXIT_H = 8,

    UI_PANEL_X = 6,
    UI_PANEL_Y = 18,
    UI_PANEL_W = 308,
    UI_PANEL_H = 176,
    UI_PANEL_TITLE_H = 10,

    UI_BG = 18,
    UI_TOP = 1,
    UI_SHADOW = 8,
    UI_PANEL = 7,
    UI_FIELD = 15,
    UI_TEXT = 1,
    UI_TEXT_DIM = 8,
    UI_TEXT_INV = 15,
    UI_ACCENT = 9,
    UI_ACCENT_ALT = 11,
    UI_ACCENT_STRONG = 13,
    UI_HOT = 14,
    UI_DANGER = 12,
    UI_OK = 10,
};

static inline void ui_border(int x, int y, int w, int h, int light, int dark) {
    gfx_fill_rect(x, y, w, 1, light);
    gfx_fill_rect(x, y, 1, h, light);
    gfx_fill_rect(x, y + h - 1, w, 1, dark);
    gfx_fill_rect(x + w - 1, y, 1, h, dark);
}

static inline void ui_text_clip(int x, int y, const char *s, int chars, int fg, int bg) {
    char tmp[40];
    int i = 0;
    while (s && s[i] && i < chars && i < (int)sizeof(tmp) - 1) {
        tmp[i] = s[i];
        i++;
    }
    tmp[i] = 0;
    gfx_text(x, y, tmp, fg, bg);
}

static inline void ui_topbar(const char *title, int exit_hot) {
    gfx_fill_rect(0, 0, UI_SW, UI_TOP_H, UI_TOP);
    gfx_fill_rect(0, UI_TOP_H - 1, UI_SW, 1, UI_SHADOW);
    gfx_text(5, 3, title, UI_TEXT_INV, -1);
    gfx_fill_rect(UI_EXIT_X, UI_EXIT_Y, UI_EXIT_W, UI_EXIT_H,
                  exit_hot ? UI_HOT : UI_DANGER);
    ui_border(UI_EXIT_X, UI_EXIT_Y, UI_EXIT_W, UI_EXIT_H, UI_TEXT_INV, 0);
    gfx_text(UI_EXIT_X + 6, UI_EXIT_Y + 1, "EXIT", UI_TEXT_INV, -1);
}

static inline void ui_panel(int x, int y, int w, int h, const char *title, int accent) {
    gfx_fill_rect(x, y, w, h, UI_PANEL);
    ui_border(x, y, w, h, UI_TEXT_INV, 0);
    gfx_fill_rect(x + 1, y + 1, w - 2, UI_PANEL_TITLE_H, accent);
    gfx_text(x + 6, y + 3, title, UI_TEXT_INV, -1);
}

static inline void ui_button(int x, int y, int w, int h,
                             const char *label, int hot, int active) {
    int fill = active ? UI_ACCENT : (hot ? UI_FIELD : UI_PANEL);
    int fg = active ? UI_TEXT_INV : UI_TEXT;
    gfx_fill_rect(x, y, w, h, fill);
    ui_border(x, y, w, h, hot ? UI_HOT : UI_TEXT_INV, active ? 0 : UI_SHADOW);
    gfx_text(x + 6, y + (h - 7) / 2, label, fg, -1);
}

static inline void ui_field(int x, int y, int w, int h, int hot, int active) {
    gfx_fill_rect(x, y, w, h, UI_FIELD);
    ui_border(x, y, w, h,
              active ? UI_HOT : (hot ? UI_ACCENT_ALT : UI_FIELD),
              active ? UI_TOP : UI_SHADOW);
}

static inline void ui_textbox(int x, int y, int w, int h,
                              const char *label, const char *text,
                              const char *placeholder, int chars,
                              int hot, int active, int cursor,
                              unsigned int frame) {
    if (label && label[0])
        gfx_text(x, y - 10, label, UI_TEXT, -1);
    ui_field(x, y, w, h, hot, active);
    if ((!text || !text[0]) && !active)
        gfx_text(x + 5, y + 5, placeholder, UI_TEXT_DIM, -1);
    else
        ui_text_clip(x + 5, y + 5, text, chars, UI_TEXT, -1);
    if (active && ((frame / 20u) & 1u) == 0u) {
        int cx = x + 5 + cursor * 6;
        if (cx > x + w - 5)
            cx = x + w - 5;
        gfx_fill_rect(cx, y + 4, 1, h > 12 ? h - 7 : 8, UI_TEXT);
    }
}

static inline void ui_pointer(int x, int y) {
    gfx_fill_rect(x, y, 1, 13, 0);
    gfx_fill_rect(x + 1, y + 1, 1, 11, 0);
    gfx_fill_rect(x + 2, y + 2, 1, 9, UI_TEXT_INV);
    gfx_fill_rect(x + 3, y + 3, 1, 7, UI_TEXT_INV);
    gfx_fill_rect(x + 4, y + 4, 1, 5, UI_TEXT_INV);
    gfx_fill_rect(x + 5, y + 5, 1, 3, 0);
    gfx_fill_rect(x, y + 13, 5, 1, 0);
}

#endif
