#include "libc.h"
#include "gui_style.h"

#define CTRL_C 0x03
#define CTRL_S 0x13
#define CTRL_L 0x0C
#define CALC_PATH "/fs/apps/calc.cfg"
#define CALC_INT_MAX 2147483647
#define CALC_INT_MIN (-2147483647 - 1)

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
    uint32_t u;
    if (v < 0) {
        append_char(dst, '-', cap);
        u = 0u - (uint32_t)v;
    } else {
        u = (uint32_t)v;
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
    int negative = 0;
    uint32_t value = 0;
    uint32_t limit;
    int digits = 0;
    if (*s == '-') {
        negative = 1;
        s++;
    }
    limit = negative ? 2147483648u : 2147483647u;
    while (*s >= '0' && *s <= '9') {
        uint32_t digit = (uint32_t)(*s - '0');
        if (value > (limit - digit) / 10u)
            return 0;
        value = value * 10u + digit;
        digits++;
        s++;
    }
    if (!digits || *s)
        return 0;
    if (negative) {
        if (value == 2147483648u)
            *out = CALC_INT_MIN;
        else
            *out = -(int)value;
    } else {
        *out = (int)value;
    }
    return 1;
}

static int checked_add(int a, int b, int *out) {
    if ((b > 0 && a > CALC_INT_MAX - b) ||
        (b < 0 && a < CALC_INT_MIN - b))
        return 0;
    *out = a + b;
    return 1;
}

static int checked_sub(int a, int b, int *out) {
    if ((b < 0 && a > CALC_INT_MAX + b) ||
        (b > 0 && a < CALC_INT_MIN + b))
        return 0;
    *out = a - b;
    return 1;
}

static int checked_mul(int a, int b, int *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return 1;
    }
    if ((a == CALC_INT_MIN && b == -1) ||
        (b == CALC_INT_MIN && a == -1))
        return 0;
    if (a > 0) {
        if (b > 0) {
            if (a > CALC_INT_MAX / b)
                return 0;
        } else if (b < CALC_INT_MIN / a) {
            return 0;
        }
    } else {
        if (b > 0) {
            if (a < CALC_INT_MIN / b)
                return 0;
        } else if (a < CALC_INT_MAX / b) {
            return 0;
        }
    }
    *out = a * b;
    return 1;
}

static int checked_div(int a, int b, int *out) {
    if (a == CALC_INT_MIN && b == -1)
        return 0;
    *out = a / b;
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
    if (op == OP_ADD) {
        if (!checked_add(a, b, &r)) {
            copy_text(result_text, "OVERFLOW", sizeof(result_text));
            copy_text(status_text, "INTEGER OVERFLOW", sizeof(status_text));
            return;
        }
    } else if (op == OP_SUB) {
        if (!checked_sub(a, b, &r)) {
            copy_text(result_text, "OVERFLOW", sizeof(result_text));
            copy_text(status_text, "INTEGER OVERFLOW", sizeof(status_text));
            return;
        }
    } else if (op == OP_MUL) {
        if (!checked_mul(a, b, &r)) {
            copy_text(result_text, "OVERFLOW", sizeof(result_text));
            copy_text(status_text, "INTEGER OVERFLOW", sizeof(status_text));
            return;
        }
    } else {
        if (b == 0) {
            copy_text(result_text, "DIV ZERO", sizeof(result_text));
            copy_text(status_text, "B CANNOT BE ZERO", sizeof(status_text));
            return;
        }
        if (!checked_div(a, b, &r)) {
            copy_text(result_text, "OVERFLOW", sizeof(result_text));
            copy_text(status_text, "DIVISION OVERFLOW", sizeof(status_text));
            return;
        }
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

static void draw_field(int index) {
    struct field *f = &fields[index];
    int hot = inside(pointer_x, pointer_y, FIELD_X, f->y, FIELD_W, FIELD_H);
    int active = focused == index;

    ui_textbox(FIELD_X, f->y, FIELD_W, FIELD_H, f->label,
               f->text, "INTEGER", FIELD_MAX, hot, active, f->cursor, frame);
}

static void draw_ops(void) {
    ui_button(OP_X, OP_Y, OP_W, OP_H, "+",
              inside(pointer_x, pointer_y, OP_X, OP_Y, OP_W, OP_H), op == OP_ADD);
    ui_button(OP_X + OP_STEP, OP_Y, OP_W, OP_H, "-",
              inside(pointer_x, pointer_y, OP_X + OP_STEP, OP_Y, OP_W, OP_H), op == OP_SUB);
    ui_button(OP_X + OP_STEP * 2, OP_Y, OP_W, OP_H, "X",
              inside(pointer_x, pointer_y, OP_X + OP_STEP * 2, OP_Y, OP_W, OP_H), op == OP_MUL);
    ui_button(OP_X + OP_STEP * 3, OP_Y, OP_W, OP_H, "/",
              inside(pointer_x, pointer_y, OP_X + OP_STEP * 3, OP_Y, OP_W, OP_H), op == OP_DIV);
}

static void draw_result(void) {
    char line[48];
    ui_panel(RESULT_X, RESULT_Y, RESULT_W, RESULT_H, "RESULT", UI_ACCENT_ALT);

    line[0] = 0;
    append_text(line, a_text[0] ? a_text : "?", sizeof(line));
    append_char(line, ' ', sizeof(line));
    append_text(line, op_label(), sizeof(line));
    append_char(line, ' ', sizeof(line));
    append_text(line, b_text[0] ? b_text : "?", sizeof(line));
    append_text(line, " =", sizeof(line));
    ui_text_clip(RESULT_X + 7, RESULT_Y + 18, line, 18, 1, -1);
    ui_text_clip(RESULT_X + 7, RESULT_Y + 31, result_text, 18, UI_OK, -1);
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

static void draw_status(void) {
    char line[64];
    const char *state = saved_flash > 0 ? "SAVED" : (dirty ? "DIRTY" : "LOADED");
    line[0] = 0;
    append_text(line, state, sizeof(line));
    append_text(line, "  ", sizeof(line));
    append_text(line, status_text, sizeof(line));
    ui_text_clip(12, 178, line, 49, saved_flash > 0 ? UI_OK : (dirty ? UI_DANGER : UI_ACCENT), -1);
}

static void draw(void) {
    gfx_clear(UI_BG);
    ui_topbar("CALC",
              inside(pointer_x, pointer_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H));
    ui_panel(6, 18, 308, 176, "TEXTBOX CALCULATOR", UI_ACCENT);

    draw_field(0);
    draw_field(1);
    draw_ops();
    draw_result();

    ui_button(SAVE_X, BTN_Y, BTN_W, BTN_H, "SAVE",
              inside(pointer_x, pointer_y, SAVE_X, BTN_Y, BTN_W, BTN_H), 0);
    ui_button(LOAD_X, BTN_Y, BTN_W, BTN_H, "LOAD",
              inside(pointer_x, pointer_y, LOAD_X, BTN_Y, BTN_W, BTN_H), 0);
    ui_button(CLEAR_X, BTN_Y, BTN_W + 8, BTN_H, "CLEAR",
              inside(pointer_x, pointer_y, CLEAR_X, BTN_Y, BTN_W + 8, BTN_H), 0);
    ui_button(EQUAL_X, BTN_Y, EQUAL_W, BTN_H, "EQUAL",
              inside(pointer_x, pointer_y, EQUAL_X, BTN_Y, EQUAL_W, BTN_H), 1);
    draw_status();
    ui_pointer(pointer_x, pointer_y);
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
