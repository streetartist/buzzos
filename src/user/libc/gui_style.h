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

struct ui_scroll {
    int first;
    int selected;
    int last_wheel;
    uint32_t last_wheel_seq;
};

static inline int ui_inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static inline void ui_center_origin(int design_w, int design_h,
                                    struct gfx_info *screen_out) {
    struct gfx_info info;
    if (gfx_info(&info) < 0) {
        gfx_set_origin(0, 0);
        if (screen_out) {
            screen_out->width = design_w;
            screen_out->height = design_h;
            screen_out->pitch = design_w;
            screen_out->bpp = 8;
        }
        return;
    }
    {
        int ox = 0;
        int oy = 0;
        if ((int)info.width > design_w)
            ox = ((int)info.width - design_w) / 2;
        if ((int)info.height > design_h)
            oy = ((int)info.height - design_h) / 2;
        gfx_set_origin(ox, oy);
    }
    if (screen_out)
        *screen_out = info;
}

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

static inline int ui_visible_rows(int h, int row_h) {
    if (h <= 0 || row_h <= 0)
        return 0;
    return h / row_h;
}

static inline int ui_scroll_max(int total, int visible) {
    int max_first = total - visible;
    return max_first > 0 ? max_first : 0;
}

static inline void ui_scroll_clamp(struct ui_scroll *scroll,
                                   int total, int visible) {
    int max_first = ui_scroll_max(total, visible);
    if (!scroll)
        return;
    if (scroll->selected < 0)
        scroll->selected = 0;
    if (total > 0 && scroll->selected >= total)
        scroll->selected = total - 1;
    if (total <= 0)
        scroll->selected = 0;
    if (scroll->first < 0)
        scroll->first = 0;
    if (scroll->first > max_first)
        scroll->first = max_first;
}

static inline void ui_scroll_reveal(struct ui_scroll *scroll,
                                    int total, int visible) {
    if (!scroll || visible <= 0)
        return;
    if (scroll->selected < scroll->first)
        scroll->first = scroll->selected;
    if (scroll->selected >= scroll->first + visible)
        scroll->first = scroll->selected - visible + 1;
    ui_scroll_clamp(scroll, total, visible);
}

static inline void ui_scroll_select(struct ui_scroll *scroll, int selected,
                                    int total, int visible) {
    if (!scroll)
        return;
    scroll->selected = selected;
    ui_scroll_reveal(scroll, total, visible);
}

static inline void ui_scroll_select_delta(struct ui_scroll *scroll, int delta,
                                          int total, int visible) {
    if (!scroll)
        return;
    ui_scroll_select(scroll, scroll->selected + delta, total, visible);
}

static inline int ui_mouse_wheel_delta(const struct mouse_state *m,
                                       struct ui_scroll *scroll) {
    int delta;
    if (!m || !scroll || m->wheel_seq == scroll->last_wheel_seq)
        return 0;
    if (scroll->last_wheel_seq == 0 && m->wheel_seq > 1) {
        scroll->last_wheel = m->wheel;
        scroll->last_wheel_seq = m->wheel_seq;
        return 0;
    }
    delta = m->wheel - scroll->last_wheel;
    scroll->last_wheel = m->wheel;
    scroll->last_wheel_seq = m->wheel_seq;
    return delta;
}

static inline int ui_wheel_to_rows(int wheel_delta) {
    int rows = -wheel_delta;
    if (wheel_delta == 0)
        return 0;
    if (rows > 8)
        rows = 8;
    if (rows < -8)
        rows = -8;
    return rows;
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

static inline void ui_list_row(int x, int y, int w, int h,
                               const char *label, int hot, int selected) {
    int fill = selected ? UI_ACCENT : (hot ? UI_FIELD : UI_PANEL);
    int fg = selected ? UI_TEXT_INV : UI_TEXT;
    int chars = (w - 12) / 6;
    if (chars < 1)
        chars = 1;
    gfx_fill_rect(x, y, w, h, fill);
    ui_border(x, y, w, h, selected ? UI_HOT : UI_TEXT_INV,
              selected ? 0 : UI_SHADOW);
    ui_text_clip(x + 6, y + (h - 7) / 2, label, chars, fg, -1);
}

static inline void ui_scrollbar(int x, int y, int h,
                                int total, int visible, int first) {
    int thumb_h;
    int thumb_y;
    int max_first = ui_scroll_max(total, visible);
    if (total <= visible || h <= 0 || max_first <= 0)
        return;
    if (first < 0)
        first = 0;
    if (first > max_first)
        first = max_first;
    gfx_fill_rect(x, y, 3, h, UI_TEXT_DIM);
    thumb_h = (h * visible) / total;
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > h)
        thumb_h = h;
    thumb_y = y + ((h - thumb_h) * first) / max_first;
    gfx_fill_rect(x, thumb_y, 3, thumb_h, UI_TEXT);
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
