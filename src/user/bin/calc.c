#include "libc.h"

#define CTRL_C 0x03
#define CTRL_S 0x13
#define CTRL_L 0x0C
#define CALC_PATH "/fs/apps/calc.cfg"

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

    FIELD_X = 22,
    FIELD_W = 118,
    FIELD_H = 16,
    FIELD_MAX = 14,

    OP_X = 168,
    OP_Y = 55,
    OP_W = 28,
    OP_H = 18,
    OP_STEP = 34,

    RESULT_X = 168,
    RESULT_Y = 92,
    RESULT_W = 126,
    RESULT_H = 42,

    SAVE_X = 22,
    LOAD_X = 82,
    CLEAR_X = 142,
    EQUAL_X = 220,
    BTN_Y = 151,
    BTN_W = 52,
    BTN_H = 15,
    EQUAL_W = 74,

    FIELD_COUNT = 2,
};

enum {
    OP_ADD = 0,
    OP_SUB,
    OP_MUL,
    OP_DIV,
};

struct field {
    const char *label;
    char *text;
    int len;
    int cursor;
    int y;
};

static char a_text[FIELD_MAX + 1];
static char b_text[FIELD_MAX + 1];

static struct field fields[FIELD_COUNT] = {
    { "A VALUE", a_text, 0, 0, 55 },
    { "B VALUE", b_text, 0, 0, 88 },
};

static int focused = 0;
static int op = OP_ADD;
static int dirty;
static int saved_flash;
static int running = 1;
static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int prev_left;
static unsigned int frame;
static uint32_t last_mouse_seq;
static char result_text[32];
static char status_text[48];

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void copy_text(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0)
        return;
    while (src && src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
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

static void append_int(char *dst, int v, int cap) {
    char tmp[12];
    int n = 0;
    unsigned int u;
    if (v < 0) {
        append_char(dst, '-', cap);
        u = (unsigned int)(-v);
    } else {
        u = (unsigned int)v;
    }
    if (u == 0) {
        append_char(dst, '0', cap);
        return;
    }
    while (u && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    }
    while (n > 0)
        append_char(dst, tmp[--n], cap);
}

static void border(int x, int y, int w, int h, int light, int dark) {
    gfx_fill_rect(x, y, w, 1, light);
    gfx_fill_rect(x, y, 1, h, light);
    gfx_fill_rect(x, y + h - 1, w, 1, dark);
    gfx_fill_rect(x + w - 1, y, 1, h, dark);
}

static void set_field(struct field *f, const char *src) {
    int n = 0;
    while (src && src[n] && src[n] != '\n' && src[n] != '\r' && n < FIELD_MAX) {
        f->text[n] = src[n];
        n++;
    }
    f->text[n] = 0;
    f->len = n;
    f->cursor = n;
}

static void mark_dirty(void) {
    dirty = 1;
    saved_flash = 0;
    copy_text(status_text, "EDITED", sizeof(status_text));
}

static int parse_int_text(const char *s, int *out) {
    int sign = 1;
    int value = 0;
    int digits = 0;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        digits++;
        s++;
    }
    if (!digits || *s)
        return 0;
    *out = sign * value;
    return 1;
}

static const char *op_label(void) {
    if (op == OP_SUB)
        return "-";
    if (op == OP_MUL)
        return "X";
    if (op == OP_DIV)
        return "/";
    return "+";
}

static char op_state_char(void) {
    if (op == OP_SUB)
        return '-';
    if (op == OP_MUL)
        return 'x';
    if (op == OP_DIV)
        return '/';
    return '+';
}

static void set_op_from_char(char ch) {
    if (ch == '-')
        op = OP_SUB;
    else if (ch == 'x' || ch == 'X' || ch == '*')
        op = OP_MUL;
    else if (ch == '/')
        op = OP_DIV;
    else
        op = OP_ADD;
}

static void calculate_result(void) {
    int a;
    int b;
    int r = 0;
    if (!parse_int_text(a_text, &a) || !parse_int_text(b_text, &b)) {
        copy_text(result_text, "INPUT ERR", sizeof(result_text));
        copy_text(status_text, "TYPE INTEGER VALUES", sizeof(status_text));
        return;
    }
    if (op == OP_ADD)
        r = a + b;
    else if (op == OP_SUB)
        r = a - b;
    else if (op == OP_MUL)
        r = a * b;
    else {
        if (b == 0) {
            copy_text(result_text, "DIV ZERO", sizeof(result_text));
            copy_text(status_text, "B CANNOT BE ZERO", sizeof(status_text));
            return;
        }
        r = a / b;
    }

    result_text[0] = 0;
    append_int(result_text, r, sizeof(result_text));
    copy_text(status_text, "READY", sizeof(status_text));
}

static const char *read_line_to(const char *p, char *dst, int cap) {
    int n = 0;
    while (p[n] && p[n] != '\n' && p[n] != '\r' && n < cap - 1) {
        dst[n] = p[n];
        n++;
    }
    dst[n] = 0;
    while (p[n] && p[n] != '\n' && p[n] != '\r')
        n++;
    while (p[n] == '\n' || p[n] == '\r')
        n++;
    return p + n;
}

static void clear_calc(void) {
    set_field(&fields[0], "");
    set_field(&fields[1], "");
    op = OP_ADD;
    copy_text(result_text, "-", sizeof(result_text));
    copy_text(status_text, "CLEARED", sizeof(status_text));
    mark_dirty();
}

static void load_calc(void) {
    char buf[128];
    int fd = open(CALC_PATH, O_RDONLY);
    if (fd < 0) {
        set_field(&fields[0], "0");
        set_field(&fields[1], "0");
        op = OP_ADD;
        calculate_result();
        dirty = 0;
        return;
    }
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;

    const char *p = buf;
    char line[24];
    if (p[0] == 'C' && p[1] == 'A' && p[2] == 'L' && p[3] == 'C') {
        while (*p && *p != '\n' && *p != '\r')
            p++;
        while (*p == '\n' || *p == '\r')
            p++;
    }
    p = read_line_to(p, line, sizeof(line));
    set_field(&fields[0], line);
    p = read_line_to(p, line, sizeof(line));
    set_field(&fields[1], line);
    p = read_line_to(p, line, sizeof(line));
    (void)p;
    set_op_from_char(line[0]);
    calculate_result();
    dirty = 0;
    saved_flash = 35;
}

static void save_calc(void) {
    char out[128];
    out[0] = 0;
    append_text(out, "CALC1\n", sizeof(out));
    append_text(out, a_text, sizeof(out));
    append_char(out, '\n', sizeof(out));
    append_text(out, b_text, sizeof(out));
    append_char(out, '\n', sizeof(out));
    append_char(out, op_state_char(), sizeof(out));
    append_char(out, '\n', sizeof(out));

    int fd = open(CALC_PATH, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0)
        return;
    int n = write(fd, out, strlen(out));
    close(fd);
    if (n > 0) {
        dirty = 0;
        saved_flash = 80;
        copy_text(status_text, "SAVED", sizeof(status_text));
    }
}

static void button(int x, int y, int w, int h, const char *label, int active) {
    int hot = inside(pointer_x, pointer_y, x, y, w, h);
    int fill = active ? 10 : (hot ? 15 : 7);
    int fg = active ? 15 : 1;
    gfx_fill_rect(x, y, w, h, fill);
    border(x, y, w, h, hot ? 14 : 15, active ? 2 : 8);
    gfx_text(x + 7, y + 5, label, fg, -1);
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
        gfx_text(FIELD_X + 5, f->y + 5, "INTEGER", 8, -1);
    else
        text_clip(FIELD_X + 5, f->y + 5, f->text, FIELD_MAX, 1, -1);

    if (active && ((frame / 20u) & 1u) == 0u) {
        int cx = FIELD_X + 5 + f->cursor * 6;
        if (cx > FIELD_X + FIELD_W - 5)
            cx = FIELD_X + FIELD_W - 5;
        gfx_fill_rect(cx, f->y + 4, 1, 9, 1);
    }
}

static void draw_ops(void) {
    button(OP_X, OP_Y, OP_W, OP_H, "+", op == OP_ADD);
    button(OP_X + OP_STEP, OP_Y, OP_W, OP_H, "-", op == OP_SUB);
    button(OP_X + OP_STEP * 2, OP_Y, OP_W, OP_H, "X", op == OP_MUL);
    button(OP_X + OP_STEP * 3, OP_Y, OP_W, OP_H, "/", op == OP_DIV);
}

static void draw_result(void) {
    char line[48];
    gfx_fill_rect(RESULT_X, RESULT_Y, RESULT_W, RESULT_H, 15);
    border(RESULT_X, RESULT_Y, RESULT_W, RESULT_H, 15, 8);
    gfx_fill_rect(RESULT_X + 1, RESULT_Y + 1, RESULT_W - 2, 11, 11);
    gfx_text(RESULT_X + 7, RESULT_Y + 3, "RESULT", 15, -1);

    line[0] = 0;
    append_text(line, a_text[0] ? a_text : "?", sizeof(line));
    append_char(line, ' ', sizeof(line));
    append_text(line, op_label(), sizeof(line));
    append_char(line, ' ', sizeof(line));
    append_text(line, b_text[0] ? b_text : "?", sizeof(line));
    append_text(line, " =", sizeof(line));
    text_clip(RESULT_X + 7, RESULT_Y + 18, line, 18, 1, -1);
    text_clip(RESULT_X + 7, RESULT_Y + 31, result_text, 18, 10, -1);
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

static int field_has_minus(struct field *f) {
    for (int i = 0; i < f->len; i++) {
        if (f->text[i] == '-')
            return 1;
    }
    return 0;
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
    if (f->len >= FIELD_MAX)
        return;
    if (ch == '-') {
        if (f->cursor != 0 || field_has_minus(f))
            return;
    } else if (ch < '0' || ch > '9') {
        return;
    }
    for (int i = f->len; i > f->cursor; i--)
        f->text[i] = f->text[i - 1];
    f->text[f->cursor++] = ch;
    f->len++;
    f->text[f->len] = 0;
    mark_dirty();
}

static void set_operator(int next_op) {
    op = next_op;
    mark_dirty();
    calculate_result();
}

static void handle_key(int key) {
    if (key == CTRL_C || key == KEY_ESC) {
        running = 0;
        return;
    }
    if (key == CTRL_S) {
        save_calc();
        return;
    }
    if (key == CTRL_L) {
        load_calc();
        return;
    }
    if (key == '\t') {
        focus_next();
        return;
    }
    if (key == '\n' || key == '\r') {
        calculate_result();
        focus_next();
        return;
    }
    if (key == '+') {
        set_operator(OP_ADD);
        return;
    }
    if (key == '/' || key == 'd' || key == 'D') {
        set_operator(OP_DIV);
        return;
    }
    if (key == 'x' || key == 'X' || key == '*') {
        set_operator(OP_MUL);
        return;
    }
    if (key == '-' && !(fields[focused].cursor == 0 && !field_has_minus(&fields[focused]))) {
        set_operator(OP_SUB);
        return;
    }
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
    for (int i = 0; i < 4; i++) {
        if (inside(x, y, OP_X + OP_STEP * i, OP_Y, OP_W, OP_H)) {
            set_operator(i);
            return;
        }
    }
    if (inside(x, y, SAVE_X, BTN_Y, BTN_W, BTN_H)) {
        save_calc();
        return;
    }
    if (inside(x, y, LOAD_X, BTN_Y, BTN_W, BTN_H)) {
        load_calc();
        return;
    }
    if (inside(x, y, CLEAR_X, BTN_Y, BTN_W, BTN_H)) {
        clear_calc();
        return;
    }
    if (inside(x, y, EQUAL_X, BTN_Y, EQUAL_W, BTN_H)) {
        calculate_result();
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
    const char *state = saved_flash > 0 ? "SAVED" : (dirty ? "DIRTY" : "LOADED");
    line[0] = 0;
    append_text(line, state, sizeof(line));
    append_text(line, "  ", sizeof(line));
    append_text(line, status_text, sizeof(line));
    text_clip(12, 178, line, 49, saved_flash > 0 ? 10 : (dirty ? 12 : 9), -1);
}

static void draw(void) {
    gfx_clear(18);
    gfx_fill_rect(0, 0, SW, TOP_H, 1);
    gfx_fill_rect(0, TOP_H - 1, SW, 1, 8);
    gfx_text(5, 3, "CALC", 15, -1);
    gfx_fill_rect(EXIT_X, EXIT_Y, EXIT_W, EXIT_H,
                  inside(pointer_x, pointer_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H) ? 14 : 12);
    border(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, 15, 0);
    gfx_text(EXIT_X + 6, EXIT_Y + 1, "EXIT", 15, -1);

    gfx_fill_rect(6, 18, 308, 176, 7);
    border(6, 18, 308, 176, 15, 0);
    gfx_fill_rect(7, 19, 306, 10, 9);
    gfx_text(12, 21, "TEXTBOX CALCULATOR", 15, -1);

    draw_field(0);
    draw_field(1);
    draw_ops();
    draw_result();

    button(SAVE_X, BTN_Y, BTN_W, BTN_H, "SAVE", 0);
    button(LOAD_X, BTN_Y, BTN_W, BTN_H, "LOAD", 0);
    button(CLEAR_X, BTN_Y, BTN_W + 8, BTN_H, "CLEAR", 0);
    button(EQUAL_X, BTN_Y, EQUAL_W, BTN_H, "EQUAL", 1);
    draw_status();
    draw_pointer();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (gfx_mode(1) < 0) {
        puts("calc: graphics mode failed");
        return 1;
    }

    load_calc();
    while (running) {
        int key;
        while ((key = read_key_poll()) >= 0)
            handle_key(key);
        handle_mouse();
        draw();
        if (saved_flash > 0)
            saved_flash--;
        frame++;
        sleep_ms(16);
    }

    gfx_mode(0);
    return 0;
}
