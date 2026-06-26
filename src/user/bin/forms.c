#include "libc.h"

#define CTRL_C 0x03
#define CTRL_S 0x13
#define CTRL_L 0x0C
#define FORM_PATH "/fs/apps/forms.cfg"

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

    FIELD_X = 18,
    FIELD_W = 154,
    FIELD_H = 16,
    FIELD_STEP = 25,
    FIELD_MAX = 23,

    PREVIEW_X = 190,
    PREVIEW_Y = 44,
    PREVIEW_W = 110,
    PREVIEW_H = 88,

    SAVE_X = 18,
    LOAD_X = 77,
    CLEAR_X = 136,
    SUBMIT_X = 208,
    BTN_Y = 151,
    BTN_W = 52,
    BTN_H = 15,
    SUBMIT_W = 72,

    FIELD_COUNT = 4,
};

struct field {
    const char *label;
    char *text;
    int len;
    int cursor;
    int max;
    int y;
};

static char name_text[FIELD_MAX + 1];
static char role_text[FIELD_MAX + 1];
static char email_text[FIELD_MAX + 1];
static char note_text[FIELD_MAX + 1];

static struct field fields[FIELD_COUNT] = {
    { "NAME", name_text, 0, 0, FIELD_MAX, 45 },
    { "ROLE", role_text, 0, 0, FIELD_MAX, 70 },
    { "EMAIL", email_text, 0, 0, FIELD_MAX, 95 },
    { "NOTE", note_text, 0, 0, FIELD_MAX, 120 },
};

static int focused = 0;
static int dirty;
static int saved_flash;
static int submit_flash;
static int running = 1;
static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int prev_left;
static unsigned int frame;
static uint32_t last_mouse_seq;

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

static void border(int x, int y, int w, int h, int light, int dark) {
    gfx_fill_rect(x, y, w, 1, light);
    gfx_fill_rect(x, y, 1, h, light);
    gfx_fill_rect(x, y + h - 1, w, 1, dark);
    gfx_fill_rect(x + w - 1, y, 1, h, dark);
}

static void set_field(struct field *f, const char *src, int len) {
    int n = 0;
    while (src && n < len && src[n] && src[n] != '\n' &&
           src[n] != '\r' && n < f->max) {
        f->text[n] = src[n];
        n++;
    }
    f->text[n] = 0;
    f->len = n;
    f->cursor = n;
}

static void clear_form(void) {
    for (int i = 0; i < FIELD_COUNT; i++)
        set_field(&fields[i], "", 0);
    dirty = 1;
    saved_flash = 0;
    submit_flash = 0;
}

static const char *next_line(const char *p, struct field *f) {
    int n = 0;
    while (p[n] && p[n] != '\n' && p[n] != '\r')
        n++;
    set_field(f, p, n);
    while (p[n] == '\n' || p[n] == '\r')
        n++;
    return p + n;
}

static void load_form(void) {
    char buf[192];
    int fd = open(FORM_PATH, O_RDONLY);
    if (fd < 0) {
        clear_form();
        dirty = 0;
        return;
    }
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;

    const char *p = buf;
    if (p[0] == 'F' && p[1] == 'O' && p[2] == 'R' && p[3] == 'M') {
        while (*p && *p != '\n' && *p != '\r')
            p++;
        while (*p == '\n' || *p == '\r')
            p++;
    }
    for (int i = 0; i < FIELD_COUNT; i++)
        p = next_line(p, &fields[i]);
    dirty = 0;
    saved_flash = 35;
    submit_flash = 0;
}

static void save_form(void) {
    char out[192];
    out[0] = 0;
    append_text(out, "FORM1\n", sizeof(out));
    for (int i = 0; i < FIELD_COUNT; i++) {
        append_text(out, fields[i].text, sizeof(out));
        append_char(out, '\n', sizeof(out));
    }

    int fd = open(FORM_PATH, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0)
        return;
    int n = write(fd, out, strlen(out));
    close(fd);
    if (n > 0) {
        dirty = 0;
        saved_flash = 80;
    }
}

static void submit_form(void) {
    save_form();
    submit_flash = 100;
}

static void mark_dirty(void) {
    dirty = 1;
    saved_flash = 0;
    submit_flash = 0;
}

static void button(int x, int y, int w, int h, const char *label, int active) {
    int hot = inside(pointer_x, pointer_y, x, y, w, h);
    int fill = active ? 10 : (hot ? 15 : 7);
    int fg = active ? 15 : 1;
    gfx_fill_rect(x, y, w, h, fill);
    border(x, y, w, h, hot ? 14 : 15, active ? 2 : 8);
    gfx_text(x + 6, y + 4, label, fg, -1);
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

static void draw_field(int index) {
    struct field *f = &fields[index];
    int hot = inside(pointer_x, pointer_y, FIELD_X, f->y, FIELD_W, FIELD_H);
    int active = focused == index;

    gfx_text(FIELD_X, f->y - 10, f->label, 1, -1);
    gfx_fill_rect(FIELD_X, f->y, FIELD_W, FIELD_H, 15);
    border(FIELD_X, f->y, FIELD_W, FIELD_H,
           active ? 14 : (hot ? 11 : 15),
           active ? 1 : 8);

    if (f->len == 0 && !active)
        gfx_text(FIELD_X + 5, f->y + 5, "EMPTY", 8, -1);
    else
        text_clip(FIELD_X + 5, f->y + 5, f->text, f->max, 1, -1);

    if (active && ((frame / 20u) & 1u) == 0u) {
        int cx = FIELD_X + 5 + f->cursor * 6;
        if (cx > FIELD_X + FIELD_W - 5)
            cx = FIELD_X + FIELD_W - 5;
        gfx_fill_rect(cx, f->y + 4, 1, 9, 1);
    }
}

static void draw_preview_line(int y, const char *label, const char *value) {
    char line[40];
    line[0] = 0;
    append_text(line, label, sizeof(line));
    append_text(line, ": ", sizeof(line));
    append_text(line, value[0] ? value : "-", sizeof(line));
    text_clip(PREVIEW_X + 7, y, line, 17, 1, -1);
}

static void draw_preview(void) {
    gfx_fill_rect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, 15);
    border(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, 15, 8);
    gfx_fill_rect(PREVIEW_X + 1, PREVIEW_Y + 1, PREVIEW_W - 2, 11, 11);
    gfx_text(PREVIEW_X + 7, PREVIEW_Y + 3, "LIVE PREVIEW", 15, -1);
    draw_preview_line(PREVIEW_Y + 20, "NAME", name_text);
    draw_preview_line(PREVIEW_Y + 34, "ROLE", role_text);
    draw_preview_line(PREVIEW_Y + 48, "EMAIL", email_text);
    draw_preview_line(PREVIEW_Y + 62, "NOTE", note_text);
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

static void focus_next(void) {
    focused++;
    if (focused >= FIELD_COUNT)
        focused = 0;
}

static void field_set_cursor(struct field *f, int cursor) {
    if (cursor < 0)
        cursor = 0;
    if (cursor > f->len)
        cursor = f->len;
    f->cursor = cursor;
}

static void field_backspace(struct field *f) {
    if (f->cursor <= 0 || f->len <= 0)
        return;
    for (int i = f->cursor; i <= f->len; i++)
        f->text[i - 1] = f->text[i];
    f->cursor--;
    f->len--;
    mark_dirty();
}

static void field_delete(struct field *f) {
    if (f->cursor >= f->len)
        return;
    for (int i = f->cursor + 1; i <= f->len; i++)
        f->text[i - 1] = f->text[i];
    f->len--;
    mark_dirty();
}

static void field_insert(struct field *f, char ch) {
    if (f->len >= f->max)
        return;
    for (int i = f->len; i > f->cursor; i--)
        f->text[i] = f->text[i - 1];
    f->text[f->cursor++] = ch;
    f->len++;
    f->text[f->len] = 0;
    mark_dirty();
}

static void handle_key(int key) {
    if (key == CTRL_C || key == KEY_ESC) {
        running = 0;
        return;
    }
    if (key == CTRL_S) {
        save_form();
        return;
    }
    if (key == CTRL_L) {
        load_form();
        return;
    }
    if (key == '\t' || key == '\n' || key == '\r') {
        focus_next();
        return;
    }
    if (focused < 0 || focused >= FIELD_COUNT)
        return;
    if (key == KEY_LEFT) {
        field_set_cursor(&fields[focused], fields[focused].cursor - 1);
        return;
    }
    if (key == KEY_RIGHT) {
        field_set_cursor(&fields[focused], fields[focused].cursor + 1);
        return;
    }
    if (key == KEY_HOME) {
        field_set_cursor(&fields[focused], 0);
        return;
    }
    if (key == KEY_END) {
        field_set_cursor(&fields[focused], fields[focused].len);
        return;
    }
    if (key == KEY_DELETE) {
        field_delete(&fields[focused]);
        return;
    }
    if (key == '\b' || key == 0x7F) {
        field_backspace(&fields[focused]);
        return;
    }
    if (key >= 32 && key < 127)
        field_insert(&fields[focused], (char)key);
}

static void click_at(int x, int y) {
    if (inside(x, y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        running = 0;
        return;
    }
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (inside(x, y, FIELD_X, fields[i].y, FIELD_W, FIELD_H)) {
            int cursor = (x - (FIELD_X + 5) + 3) / 6;
            focused = i;
            field_set_cursor(&fields[i], cursor);
            return;
        }
    }
    if (inside(x, y, SAVE_X, BTN_Y, BTN_W, BTN_H)) {
        save_form();
        return;
    }
    if (inside(x, y, LOAD_X, BTN_Y, BTN_W, BTN_H)) {
        load_form();
        return;
    }
    if (inside(x, y, CLEAR_X, BTN_Y, BTN_W, BTN_H)) {
        clear_form();
        return;
    }
    if (inside(x, y, SUBMIT_X, BTN_Y, SUBMIT_W, BTN_H)) {
        submit_form();
        return;
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

static void draw_status(void) {
    char line[64];
    const char *status = submit_flash > 0 ? "SUBMITTED" :
                         (saved_flash > 0 ? "SAVED" :
                         (dirty ? "DIRTY" : "LOADED"));
    line[0] = 0;
    append_text(line, status, sizeof(line));
    append_text(line, "  ", sizeof(line));
    append_text(line, FORM_PATH, sizeof(line));
    text_clip(12, 178, line, 49,
              submit_flash > 0 ? 10 : (saved_flash > 0 ? 10 : (dirty ? 12 : 9)),
              -1);
}

static void draw(void) {
    gfx_clear(18);
    gfx_fill_rect(0, 0, SW, TOP_H, 1);
    gfx_fill_rect(0, TOP_H - 1, SW, 1, 8);
    gfx_text(5, 3, "FORMS", 15, -1);
    gfx_fill_rect(EXIT_X, EXIT_Y, EXIT_W, EXIT_H,
                  inside(pointer_x, pointer_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H) ? 14 : 12);
    border(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, 15, 0);
    gfx_text(EXIT_X + 6, EXIT_Y + 1, "EXIT", 15, -1);

    gfx_fill_rect(6, 18, 308, 176, 7);
    border(6, 18, 308, 176, 15, 0);
    gfx_fill_rect(7, 19, 306, 10, 13);
    gfx_text(12, 21, "TEXT INPUT FORM", 15, -1);

    for (int i = 0; i < FIELD_COUNT; i++)
        draw_field(i);
    draw_preview();

    button(SAVE_X, BTN_Y, BTN_W, BTN_H, "SAVE", 0);
    button(LOAD_X, BTN_Y, BTN_W, BTN_H, "LOAD", 0);
    button(CLEAR_X, BTN_Y, BTN_W + 8, BTN_H, "CLEAR", 0);
    button(SUBMIT_X, BTN_Y, SUBMIT_W, BTN_H, "SUBMIT", submit_flash > 0);
    draw_status();
    draw_pointer();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (gfx_mode(1) < 0) {
        puts("forms: graphics mode failed");
        return 1;
    }

    load_form();
    while (running) {
        int key;
        while ((key = read_key_poll()) >= 0)
            handle_key(key);
        handle_mouse();
        draw();
        if (saved_flash > 0)
            saved_flash--;
        if (submit_flash > 0)
            submit_flash--;
        frame++;
        sleep_ms(16);
    }

    gfx_mode(0);
    return 0;
}
