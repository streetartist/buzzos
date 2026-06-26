#include "libc.h"

#define CTRL_C 0x03
#define CFG_PATH "/fs/apps/guidemo.cfg"

enum {
    KEY_ESC = 0x1B,
    KEY_LEFT = 256,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

enum {
    SW = 320,
    SH = 200,
    TOP_H = 12,

    EXIT_X = 286,
    EXIT_Y = 2,
    EXIT_W = 28,
    EXIT_H = 8,

    BTN_X = 18,
    BTN_Y = 44,
    BTN_W = 66,
    BTN_H = 16,

    TOGGLE_X = 18,
    TOGGLE_Y = 71,
    TOGGLE_W = 66,
    TOGGLE_H = 16,

    SAVE_X = 18,
    SAVE_Y = 98,
    SAVE_W = 66,
    SAVE_H = 16,

    RESET_X = 18,
    RESET_Y = 125,
    RESET_W = 66,
    RESET_H = 16,

    SWATCH_X = 128,
    SWATCH_Y = 44,
    SWATCH_W = 16,
    SWATCH_H = 16,

    INPUT_X = 128,
    INPUT_Y = 111,
    INPUT_W = 174,
    INPUT_H = 16,
    INPUT_MAX = 26,
};

static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int prev_left;
static int running = 1;
static int clicks;
static int toggle_on = 1;
static int color_index = 0;
static int dirty;
static int saved_flash;
static int input_focused;
static int input_len;
static int input_cursor;
static unsigned int frame;
static uint32_t last_mouse_seq;
static char input_text[INPUT_MAX + 1];

static const uint8_t colors[] = { 10, 11, 12, 14, 9, 13 };

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void append_char(char *dst, char ch, int cap) {
    int n = (int)strlen(dst);
    if (n < cap - 1) {
        dst[n++] = ch;
        dst[n] = 0;
    }
}

static void append_text(char *dst, const char *src, int cap) {
    int n = (int)strlen(dst);
    int i = 0;
    while (src && src[i] && n < cap - 1)
        dst[n++] = src[i++];
    dst[n] = 0;
}

static void append_uint(char *dst, unsigned int v, int cap) {
    char tmp[12];
    int n = 0;
    if (v == 0) {
        append_char(dst, '0', cap);
        return;
    }
    while (v && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0)
        append_char(dst, tmp[--n], cap);
}

static const char *parse_uint(const char *s, int *out) {
    int v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    *out = v;
    return s;
}

static void set_input_text(const char *s) {
    input_len = 0;
    while (s && s[input_len] && s[input_len] != '\n' &&
           s[input_len] != '\r' && input_len < INPUT_MAX) {
        input_text[input_len] = s[input_len];
        input_len++;
    }
    input_text[input_len] = 0;
    input_cursor = input_len;
}

static void mark_dirty(void) {
    dirty = 1;
    saved_flash = 0;
}

static void reset_state(void) {
    clicks = 0;
    toggle_on = 1;
    color_index = 0;
    set_input_text("");
    input_focused = 0;
    mark_dirty();
}

static void load_state(void) {
    char buf[128];
    int fd = open(CFG_PATH, O_RDONLY);
    if (fd < 0)
        return;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;

    int c = 0;
    int t = 0;
    int col = 0;
    const char *p = parse_uint(buf, &c);
    if (!p)
        return;
    p = parse_uint(p, &t);
    if (!p)
        return;
    p = parse_uint(p, &col);
    if (!p)
        return;
    if (col < 0 || col >= (int)(sizeof(colors) / sizeof(colors[0])))
        col = 0;
    clicks = c;
    toggle_on = t ? 1 : 0;
    color_index = col;

    while (*p && *p != '\n' && *p != '\r')
        p++;
    while (*p == '\n' || *p == '\r')
        p++;
    set_input_text(p);
    dirty = 0;
}

static void save_state(void) {
    char line[128];
    line[0] = 0;
    append_uint(line, (unsigned int)clicks, sizeof(line));
    append_char(line, ' ', sizeof(line));
    append_uint(line, (unsigned int)toggle_on, sizeof(line));
    append_char(line, ' ', sizeof(line));
    append_uint(line, (unsigned int)color_index, sizeof(line));
    append_char(line, '\n', sizeof(line));
    append_text(line, input_text, sizeof(line));
    append_char(line, '\n', sizeof(line));

    int fd = open(CFG_PATH, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0)
        return;
    int n = write(fd, line, strlen(line));
    close(fd);
    if (n > 0) {
        dirty = 0;
        saved_flash = 80;
    }
}

static void border(int x, int y, int w, int h, int light, int dark) {
    gfx_fill_rect(x, y, w, 1, light);
    gfx_fill_rect(x, y, 1, h, light);
    gfx_fill_rect(x, y + h - 1, w, 1, dark);
    gfx_fill_rect(x + w - 1, y, 1, h, dark);
}

static void button(int x, int y, int w, int h, const char *label, int active) {
    int hot = inside(pointer_x, pointer_y, x, y, w, h);
    int fill = active ? 9 : (hot ? 15 : 7);
    int fg = active ? 15 : 1;
    gfx_fill_rect(x, y, w, h, fill);
    border(x, y, w, h, hot ? 14 : 15, active ? 0 : 8);
    gfx_text(x + 6, y + 5, label, fg, -1);
}

static void text_clip(int x, int y, const char *s, int chars, int fg, int bg) {
    char tmp[32];
    int i = 0;
    while (s && s[i] && i < chars && i < (int)sizeof(tmp) - 1) {
        tmp[i] = s[i];
        i++;
    }
    tmp[i] = 0;
    gfx_text(x, y, tmp, fg, bg);
}

static void draw_textbox(void) {
    int hot = inside(pointer_x, pointer_y, INPUT_X, INPUT_Y, INPUT_W, INPUT_H);
    gfx_text(INPUT_X, INPUT_Y - 11, "TEXT INPUT", 1, -1);
    gfx_fill_rect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, 15);
    border(INPUT_X, INPUT_Y, INPUT_W, INPUT_H,
           input_focused ? 14 : (hot ? 11 : 15),
           input_focused ? 1 : 8);

    if (input_len == 0 && !input_focused) {
        gfx_text(INPUT_X + 5, INPUT_Y + 5, "TYPE HERE", 8, -1);
    } else {
        text_clip(INPUT_X + 5, INPUT_Y + 5, input_text, INPUT_MAX, 1, -1);
    }

    if (input_focused && ((frame / 20u) & 1u) == 0u) {
        int cx = INPUT_X + 5 + input_cursor * 6;
        if (cx > INPUT_X + INPUT_W - 5)
            cx = INPUT_X + INPUT_W - 5;
        gfx_fill_rect(cx, INPUT_Y + 4, 1, 9, 1);
    }
}

static int read_byte_poll(void) {
    unsigned char c;
    int n = read(0, &c, 1);
    if (n > 0)
        return c;
    return -1;
}

static int read_key_poll(void) {
    int c = read_byte_poll();
    if (c != KEY_ESC)
        return c;

    int b = read_byte_poll();
    if (b < 0 || b != '[')
        return KEY_ESC;

    int k = read_byte_poll();
    switch (k) {
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '3':
        if (read_byte_poll() == '~')
            return KEY_DELETE;
        return KEY_ESC;
    default:
        return KEY_ESC;
    }
}

static void input_set_cursor(int cursor) {
    if (cursor < 0)
        cursor = 0;
    if (cursor > input_len)
        cursor = input_len;
    input_cursor = cursor;
}

static void input_backspace(void) {
    if (input_cursor <= 0 || input_len <= 0)
        return;
    for (int i = input_cursor; i <= input_len; i++)
        input_text[i - 1] = input_text[i];
    input_cursor--;
    input_len--;
    mark_dirty();
}

static void input_delete(void) {
    if (input_cursor >= input_len)
        return;
    for (int i = input_cursor + 1; i <= input_len; i++)
        input_text[i - 1] = input_text[i];
    input_len--;
    mark_dirty();
}

static void input_insert(char ch) {
    if (input_len >= INPUT_MAX)
        return;
    for (int i = input_len; i > input_cursor; i--)
        input_text[i] = input_text[i - 1];
    input_text[input_cursor++] = ch;
    input_len++;
    input_text[input_len] = 0;
    mark_dirty();
}

static void handle_key(int key) {
    if (key == CTRL_C) {
        running = 0;
        return;
    }
    if (input_focused) {
        if (key == KEY_ESC || key == '\n' || key == '\r') {
            input_focused = 0;
            return;
        }
        if (key == KEY_LEFT) {
            input_set_cursor(input_cursor - 1);
            return;
        }
        if (key == KEY_RIGHT) {
            input_set_cursor(input_cursor + 1);
            return;
        }
        if (key == KEY_HOME) {
            input_set_cursor(0);
            return;
        }
        if (key == KEY_END) {
            input_set_cursor(input_len);
            return;
        }
        if (key == KEY_DELETE) {
            input_delete();
            return;
        }
        if (key == '\b' || key == 0x7F) {
            input_backspace();
            return;
        }
        if (key >= 32 && key < 127) {
            input_insert((char)key);
            return;
        }
        return;
    }
    if (key == KEY_ESC) {
        running = 0;
        return;
    }
    if (key == ' ' || key == '\n' || key == '\r')
        clicks++, mark_dirty();
    else if (key == 'i' || key == 'I') {
        input_focused = 1;
    }
    else if (key == 't' || key == 'T') {
        toggle_on = !toggle_on;
        mark_dirty();
    } else if (key == 's' || key == 'S') {
        save_state();
    } else if (key == 'r' || key == 'R') {
        reset_state();
    }
}

static void click_at(int x, int y) {
    if (inside(x, y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        running = 0;
        return;
    }
    if (inside(x, y, INPUT_X, INPUT_Y, INPUT_W, INPUT_H)) {
        int cursor = (x - (INPUT_X + 5) + 3) / 6;
        input_focused = 1;
        input_set_cursor(cursor);
        return;
    }
    input_focused = 0;
    if (inside(x, y, BTN_X, BTN_Y, BTN_W, BTN_H)) {
        clicks++;
        mark_dirty();
        return;
    }
    if (inside(x, y, TOGGLE_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H)) {
        toggle_on = !toggle_on;
        mark_dirty();
        return;
    }
    if (inside(x, y, SAVE_X, SAVE_Y, SAVE_W, SAVE_H)) {
        save_state();
        return;
    }
    if (inside(x, y, RESET_X, RESET_Y, RESET_W, RESET_H)) {
        reset_state();
        return;
    }
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        int sx = SWATCH_X + i * 20;
        if (inside(x, y, sx, SWATCH_Y, SWATCH_W, SWATCH_H)) {
            color_index = i;
            mark_dirty();
            return;
        }
    }
}

static void handle_mouse(void) {
    struct mouse_state m;
    int left = 0;
    if (mouse_get(&m) == 0) {
        if (m.seq != last_mouse_seq) {
            pointer_x = m.x;
            pointer_y = m.y;
            last_mouse_seq = m.seq;
        }
        left = (m.buttons & 1) != 0;
    }
    if (left && !prev_left)
        click_at(pointer_x, pointer_y);
    prev_left = left;
}

static void draw_pointer(void) {
    int x = pointer_x;
    int y = pointer_y;
    gfx_fill_rect(x, y, 1, 13, 0);
    gfx_fill_rect(x + 1, y + 1, 1, 11, 0);
    gfx_fill_rect(x + 2, y + 2, 1, 9, 15);
    gfx_fill_rect(x + 3, y + 3, 1, 7, 15);
    gfx_fill_rect(x + 4, y + 4, 1, 5, 15);
    gfx_fill_rect(x + 5, y + 5, 1, 3, 0);
    gfx_fill_rect(x, y + 13, 5, 1, 0);
}

static void draw_demo(void) {
    char line[64];
    int accent = colors[color_index];
    int progress = (int)((frame + (unsigned int)clicks * 12u) % 118u);

    gfx_clear(18);
    gfx_fill_rect(0, 0, SW, TOP_H, 1);
    gfx_fill_rect(0, TOP_H - 1, SW, 1, 8);
    gfx_text(5, 3, "USER GUI DEMO", 15, -1);
    gfx_fill_rect(EXIT_X, EXIT_Y, EXIT_W, EXIT_H,
                  inside(pointer_x, pointer_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H) ? 14 : 12);
    border(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, 15, 0);
    gfx_text(EXIT_X + 6, EXIT_Y + 1, "EXIT", 15, -1);

    gfx_fill_rect(6, 18, 308, 176, 7);
    border(6, 18, 308, 176, 15, 0);
    gfx_fill_rect(7, 19, 306, 10, accent);
    gfx_text(12, 21, "SAMPLE CONTROLS", 15, -1);

    button(BTN_X, BTN_Y, BTN_W, BTN_H, "CLICK", 0);
    button(TOGGLE_X, TOGGLE_Y, TOGGLE_W, TOGGLE_H, toggle_on ? "ON" : "OFF", toggle_on);
    button(SAVE_X, SAVE_Y, SAVE_W, SAVE_H, "SAVE", 0);
    button(RESET_X, RESET_Y, RESET_W, RESET_H, "RESET", 0);

    line[0] = 0;
    append_text(line, "CLICKS ", sizeof(line));
    append_uint(line, (unsigned int)clicks, sizeof(line));
    gfx_text(18, 150, line, 0, -1);

    gfx_text(SWATCH_X, 33, "COLOR", 0, -1);
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        int sx = SWATCH_X + i * 20;
        gfx_fill_rect(sx, SWATCH_Y, SWATCH_W, SWATCH_H, colors[i]);
        border(sx, SWATCH_Y, SWATCH_W, SWATCH_H, i == color_index ? 14 : 15, 0);
    }

    gfx_text(128, 69, "ANIMATED BAR", 0, -1);
    gfx_fill_rect(128, 82, 120, 12, 15);
    border(128, 82, 120, 12, 15, 0);
    gfx_fill_rect(129, 83, progress, 10, accent);

    draw_textbox();

    gfx_text(128, 137, "STATE FILE", 1, -1);
    gfx_text(128, 150, CFG_PATH, 0, -1);
    if (saved_flash > 0)
        gfx_text(128, 163, "SAVED", 10, -1);
    else if (dirty)
        gfx_text(128, 163, "DIRTY", 12, -1);
    else
        gfx_text(128, 163, "LOADED", 9, -1);

    gfx_text(18, 166, "USER GFX APP", 1, -1);
    gfx_text(18, 177, "TEXTBOX MOUSE FS", 0, -1);

    draw_pointer();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (gfx_mode(1) < 0) {
        puts("guidemo: graphics mode failed");
        return 1;
    }
    load_state();

    while (running) {
        int key;
        while ((key = read_key_poll()) >= 0)
            handle_key(key);
        handle_mouse();
        draw_demo();
        if (saved_flash > 0)
            saved_flash--;
        frame++;
        sleep_ms(16);
    }

    gfx_mode(0);
    return 0;
}
