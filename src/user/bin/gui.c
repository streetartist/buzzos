#include "libc.h"

#define CTRL_C 0x03

enum {
    KEY_UP = 256,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
};

enum {
    SW = 320,
    SH = 200,

    TOP_H = 12,

    APP_MANAGER = 0,
    APP_PAINT = 1,
    APP_SHELL = 2,

    BACK_X = 246,
    BACK_Y = 2,
    BACK_W = 34,
    BACK_H = 8,
    EXIT_X = 286,
    EXIT_Y = 2,
    EXIT_W = 28,
    EXIT_H = 8,

    MGR_ICON_W = 84,
    MGR_ICON_H = 28,
    MGR_PAINT_X = 20,
    MGR_PAINT_Y = 28,
    MGR_SHELL_X = 118,
    MGR_SHELL_Y = 28,
    MGR_REFRESH_X = 216,
    MGR_REFRESH_Y = 28,

    PAINT_X = 4,
    PAINT_Y = 15,
    PAINT_W = 206,
    PAINT_H = 133,
    CANVAS_X = 8,
    CANVAS_Y = 29,
    CANVAS_W = 198,
    CANVAS_H = 98,
    PALETTE_Y = 132,

    SHELL_X = 4,
    SHELL_Y = 15,
    SHELL_W = 312,
    SHELL_H = 181,

    SHELL_COLS = 50,
    SHELL_LINES = 18,
    APP_MAX = 6,
};

struct app_entry {
    char name[24];
    char path[64];
};

static uint8_t fb[SW * SH];
static uint8_t canvas[CANVAS_W * CANVAS_H];
static struct app_entry apps[APP_MAX];
static int app_count;

static char shell_lines[SHELL_LINES][SHELL_COLS + 1];
static char shell_input[SHELL_COLS + 1];
static int shell_len;

static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int current_color = 0;
static int running = 1;
static int active_app = APP_MANAGER;
static int prev_left;
static int drawing;
static int last_cx;
static int last_cy;
static uint32_t last_mouse_seq;

static const uint8_t palette[] = { 0, 15, 12, 10, 11, 14, 9, 5, 6, 8 };

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

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
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

static void append_text(char *dst, const char *src, int cap) {
    int n = (int)strlen(dst);
    int i = 0;
    while (src && src[i] && n < cap - 1)
        dst[n++] = src[i++];
    dst[n] = 0;
}

static void append_char(char *dst, char ch, int cap) {
    int n = (int)strlen(dst);
    if (n < cap - 1) {
        dst[n++] = ch;
        dst[n] = 0;
    }
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

static void shell_push_line(const char *s, int len) {
    if (len < 0)
        len = (int)strlen(s);
    for (int i = 1; i < SHELL_LINES; i++)
        copy_text(shell_lines[i - 1], shell_lines[i], sizeof(shell_lines[i - 1]));
    if (len > SHELL_COLS)
        len = SHELL_COLS;
    for (int i = 0; i < len; i++)
        shell_lines[SHELL_LINES - 1][i] = s[i];
    shell_lines[SHELL_LINES - 1][len] = 0;
}

static void shell_log(const char *s) {
    char line[SHELL_COLS + 1];
    int n = 0;
    if (!s || !s[0]) {
        shell_push_line("", 0);
        return;
    }
    for (int i = 0;; i++) {
        char c = s[i];
        if (c == '\n' || c == 0 || n == SHELL_COLS) {
            line[n] = 0;
            shell_push_line(line, n);
            n = 0;
            if (c == 0)
                break;
            if (c == '\n')
                continue;
        }
        if (c != '\n' && c != 0)
            line[n++] = c;
    }
}

static const uint8_t *glyph_for(char c) {
    static const uint8_t space[7] = {0,0,0,0,0,0,0};
    static const uint8_t dash[7]  = {0,0,0,0x1F,0,0,0};
    static const uint8_t dot[7]   = {0,0,0,0,0,0x0C,0x0C};
    static const uint8_t colon[7] = {0,0x0C,0x0C,0,0x0C,0x0C,0};
    static const uint8_t slash[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    static const uint8_t gt[7]    = {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
    static const uint8_t lt[7]    = {0x01,0x02,0x04,0x08,0x04,0x02,0x01};
    static const uint8_t plus[7]  = {0,0x04,0x04,0x1F,0x04,0x04,0};
    static const uint8_t eq[7]    = {0,0,0x1F,0,0x1F,0,0};
    static const uint8_t under[7] = {0,0,0,0,0,0,0x1F};
    static const uint8_t bang[7]  = {0x04,0x04,0x04,0x04,0x04,0,0x04};
    static const uint8_t ques[7]  = {0x0E,0x11,0x01,0x02,0x04,0,0x04};
    static const uint8_t comma[7] = {0,0,0,0,0x0C,0x0C,0x08};
    static const uint8_t lpar[7]  = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    static const uint8_t rpar[7]  = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return font_upper[c - 'A'];
    if (c >= '0' && c <= '9')
        return font_digits[c - '0'];
    if (c == '-') return dash;
    if (c == '.') return dot;
    if (c == ':') return colon;
    if (c == '/') return slash;
    if (c == '>') return gt;
    if (c == '<') return lt;
    if (c == '+') return plus;
    if (c == '=') return eq;
    if (c == '_') return under;
    if (c == '!') return bang;
    if (c == '?') return ques;
    if (c == ',') return comma;
    if (c == '(') return lpar;
    if (c == ')') return rpar;
    return space;
}

static void px(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= SW || y >= SH)
        return;
    fb[y * SW + x] = color;
}

static void rect(int x, int y, int w, int h, uint8_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0)
        return;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            fb[(y + yy) * SW + x + xx] = color;
}

static void border(int x, int y, int w, int h, uint8_t light, uint8_t dark) {
    rect(x, y, w, 1, light);
    rect(x, y, 1, h, light);
    rect(x, y + h - 1, w, 1, dark);
    rect(x + w - 1, y, 1, h, dark);
}

static void text(int x, int y, const char *s, uint8_t fg, int bg) {
    while (s && *s) {
        const uint8_t *g = glyph_for(*s++);
        for (int yy = 0; yy < 7; yy++) {
            for (int xx = 0; xx < 5; xx++) {
                int on = (g[yy] & (1 << (4 - xx))) != 0;
                if (on)
                    px(x + xx, y + yy, fg);
                else if (bg >= 0)
                    px(x + xx, y + yy, (uint8_t)bg);
            }
        }
        x += 6;
    }
}

static void text_clip(int x, int y, const char *s, int chars, uint8_t fg, int bg) {
    char tmp[64];
    int i = 0;
    while (s && s[i] && i < chars && i < (int)sizeof(tmp) - 1) {
        tmp[i] = s[i];
        i++;
    }
    tmp[i] = 0;
    text(x, y, tmp, fg, bg);
}

static void canvas_px(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H)
        return;
    canvas[y * CANVAS_W + x] = color;
}

static void canvas_brush(int x, int y, uint8_t color) {
    canvas_px(x, y, color);
    canvas_px(x - 1, y, color);
    canvas_px(x + 1, y, color);
    canvas_px(x, y - 1, color);
    canvas_px(x, y + 1, color);
}

static void canvas_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        canvas_brush(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void scan_apps(void) {
    app_count = 0;
    (void)mkdir("/fs/apps");
    int fd = open("/fs/apps", O_RDONLY);
    if (fd < 0)
        return;

    struct dirent ents[4];
    int n;
    while (app_count < APP_MAX && (n = getdents(fd, ents, sizeof(ents))) > 0) {
        int count = n / (int)sizeof(struct dirent);
        for (int i = 0; i < count && app_count < APP_MAX; i++) {
            if (ents[i].d_type != DT_REG)
                continue;
            copy_text(apps[app_count].name, ents[i].d_name, sizeof(apps[app_count].name));
            copy_text(apps[app_count].path, "/fs/apps/", sizeof(apps[app_count].path));
            append_text(apps[app_count].path, ents[i].d_name, sizeof(apps[app_count].path));
            app_count++;
        }
    }
    close(fd);
}

static int launch_external(const char *path) {
    int status = -1;
    gfx_mode(0);
    char *argv[1];
    argv[0] = (char *)path;
    int pid = spawn_process_args(path, argv, 1, 1);
    if (pid >= 0)
        waitpid(pid, &status, 0);
    gfx_mode(1);
    return pid < 0 ? -1 : status;
}

static void shell_list_apps(void) {
    scan_apps();
    if (app_count == 0) {
        shell_log("APPS: EMPTY");
        return;
    }
    for (int i = 0; i < app_count; i++) {
        char line[SHELL_COLS + 1];
        line[0] = 0;
        append_uint(line, (unsigned int)i, sizeof(line));
        append_text(line, " ", sizeof(line));
        append_text(line, apps[i].name, sizeof(line));
        shell_log(line);
    }
}

static void shell_cmd_ls(const char *path) {
    path = skip_spaces(path);
    if (!path[0])
        path = "/";
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        shell_log("LS: NOT FOUND");
        return;
    }

    char line[SHELL_COLS + 1];
    line[0] = 0;
    struct dirent ents[4];
    int any = 0;
    int n;
    while ((n = getdents(fd, ents, sizeof(ents))) > 0) {
        int count = n / (int)sizeof(struct dirent);
        for (int i = 0; i < count; i++) {
            int need = (int)strlen(ents[i].d_name) + 2;
            if ((int)strlen(line) + need >= SHELL_COLS) {
                shell_log(line);
                line[0] = 0;
            }
            append_text(line, ents[i].d_name, sizeof(line));
            if (ents[i].d_type == DT_DIR)
                append_char(line, '/', sizeof(line));
            append_char(line, ' ', sizeof(line));
            any = 1;
        }
    }
    close(fd);
    if (line[0])
        shell_log(line);
    else if (!any)
        shell_log("LS: EMPTY");
}

static void shell_cmd_cat(const char *path) {
    path = skip_spaces(path);
    if (!path[0]) {
        shell_log("CAT: PATH");
        return;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        shell_log("CAT: NOT FOUND");
        return;
    }
    char buf[256];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) {
        shell_log("CAT: EMPTY");
        return;
    }
    char line[SHELL_COLS + 1];
    int pos = 0;
    int lines = 0;
    for (int i = 0; i < n && lines < 6; i++) {
        char c = buf[i];
        if (c == '\r')
            continue;
        if (c == '\n' || pos == SHELL_COLS) {
            line[pos] = 0;
            shell_log(line);
            pos = 0;
            lines++;
            if (c == '\n')
                continue;
        }
        if (c >= 32 && c < 127)
            line[pos++] = c;
    }
    if (pos > 0 && lines < 6) {
        line[pos] = 0;
        shell_log(line);
    }
}

static void shell_cmd_run(const char *path) {
    path = skip_spaces(path);
    if (!path[0]) {
        shell_log("RUN: PATH");
        return;
    }
    char msg[SHELL_COLS + 1];
    msg[0] = 0;
    append_text(msg, "RUN ", sizeof(msg));
    append_text(msg, path, sizeof(msg));
    shell_log(msg);
    int status = launch_external(path);
    if (status < 0) {
        shell_log("RUN: FAILED");
    } else {
        msg[0] = 0;
        append_text(msg, "EXIT ", sizeof(msg));
        append_uint(msg, (unsigned int)status, sizeof(msg));
        shell_log(msg);
    }
    scan_apps();
}

static void shell_execute(void) {
    const char *cmd = skip_spaces(shell_input);
    if (!cmd[0])
        return;
    char echo[SHELL_COLS + 1];
    echo[0] = 0;
    append_text(echo, "> ", sizeof(echo));
    append_text(echo, cmd, sizeof(echo));
    shell_log(echo);

    if (streq(cmd, "help")) {
        shell_log("HELP LS CAT CLEAR ECHO RUN APPS");
    } else if (streq(cmd, "clear")) {
        for (int i = 0; i < SHELL_LINES; i++)
            shell_lines[i][0] = 0;
    } else if (streq(cmd, "apps")) {
        shell_list_apps();
    } else if (starts_with(cmd, "ls")) {
        shell_cmd_ls(cmd + 2);
    } else if (starts_with(cmd, "cat ")) {
        shell_cmd_cat(cmd + 4);
    } else if (starts_with(cmd, "echo ")) {
        shell_log(cmd + 5);
    } else if (starts_with(cmd, "run ")) {
        shell_cmd_run(cmd + 4);
    } else {
        shell_log("UNKNOWN");
    }
}

static int read_raw_poll(void) {
    unsigned char c;
    int n = read(0, &c, 1);
    if (n > 0)
        return c;
    return -1;
}

static int read_key_poll(void) {
    int c = read_raw_poll();
    if (c < 0)
        return -1;
    if (c != 0x1B)
        return c;

    int c1 = -1;
    for (int i = 0; i < 8 && c1 < 0; i++) {
        c1 = read_raw_poll();
        if (c1 < 0)
            yield();
    }
    if (c1 != '[')
        return 0x1B;

    int c2 = -1;
    for (int i = 0; i < 8 && c2 < 0; i++) {
        c2 = read_raw_poll();
        if (c2 < 0)
            yield();
    }
    switch (c2) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    default: return 0x1B;
    }
}

static void handle_key(int k) {
    if (k == CTRL_C || k == 0x1B) {
        if (active_app == APP_MANAGER)
            running = 0;
        else {
            active_app = APP_MANAGER;
            scan_apps();
        }
        return;
    }
    if (k == KEY_LEFT) {
        if (pointer_x > 0) pointer_x -= 4;
        return;
    }
    if (k == KEY_RIGHT) {
        if (pointer_x < SW - 1) pointer_x += 4;
        return;
    }
    if (k == KEY_UP) {
        if (pointer_y > TOP_H) pointer_y -= 4;
        return;
    }
    if (k == KEY_DOWN) {
        if (pointer_y < SH - 1) pointer_y += 4;
        return;
    }
    if (active_app == APP_MANAGER) {
        if (k == '1' || k == 'p' || k == 'P')
            active_app = APP_PAINT;
        else if (k == '2' || k == 's' || k == 'S')
            active_app = APP_SHELL;
        else if (k == 'r' || k == 'R')
            scan_apps();
        return;
    }
    if (active_app == APP_PAINT) {
        if (k == 'c' || k == 'C') {
            for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
                canvas[i] = 15;
        }
        return;
    }
    if (active_app != APP_SHELL)
        return;
    if (k == '\r')
        k = '\n';
    if (k == '\n') {
        shell_input[shell_len] = 0;
        shell_execute();
        shell_len = 0;
        shell_input[0] = 0;
        return;
    }
    if (k == '\b' || k == 0x7F) {
        if (shell_len > 0) {
            shell_len--;
            shell_input[shell_len] = 0;
        }
        return;
    }
    if (k >= 32 && k < 127 && shell_len < SHELL_COLS - 2) {
        shell_input[shell_len++] = (char)k;
        shell_input[shell_len] = 0;
    }
}

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void handle_mouse_click(int x, int y) {
    if (inside(x, y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        running = 0;
        return;
    }

    if (active_app != APP_MANAGER && inside(x, y, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        active_app = APP_MANAGER;
        scan_apps();
        return;
    }

    if (active_app == APP_MANAGER) {
        if (inside(x, y, MGR_PAINT_X, MGR_PAINT_Y, MGR_ICON_W, MGR_ICON_H)) {
            active_app = APP_PAINT;
            return;
        }
        if (inside(x, y, MGR_SHELL_X, MGR_SHELL_Y, MGR_ICON_W, MGR_ICON_H)) {
            active_app = APP_SHELL;
            return;
        }
        if (inside(x, y, MGR_REFRESH_X, MGR_REFRESH_Y, MGR_ICON_W, MGR_ICON_H)) {
            scan_apps();
            return;
        }
        for (int i = 0; i < app_count; i++) {
            int col = i % 3;
            int row = i / 3;
            int ax = 20 + col * 98;
            int ay = 86 + row * 28;
            if (inside(x, y, ax, ay, MGR_ICON_W, MGR_ICON_H)) {
                shell_cmd_run(apps[i].path);
                active_app = APP_MANAGER;
                return;
            }
        }
        return;
    }

    if (active_app == APP_PAINT) {
        for (int i = 0; i < (int)(sizeof(palette) / sizeof(palette[0])); i++) {
            int px0 = CANVAS_X + i * 12;
            if (inside(x, y, px0, PALETTE_Y, 10, 10)) {
                current_color = i;
                return;
            }
        }
    }
}

static void handle_mouse(void) {
    struct mouse_state m;
    int left = 0;
    int right = 0;
    if (mouse_get(&m) == 0) {
        if (m.seq != last_mouse_seq) {
            pointer_x = m.x;
            pointer_y = m.y;
            last_mouse_seq = m.seq;
        }
        left = (m.buttons & 1) != 0;
        right = (m.buttons & 2) != 0;
    }

    if (left && !prev_left)
        handle_mouse_click(pointer_x, pointer_y);

    int in_canvas = active_app == APP_PAINT &&
        inside(pointer_x, pointer_y, CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H);
    if ((left || right) && in_canvas) {
        int cx = pointer_x - CANVAS_X;
        int cy = pointer_y - CANVAS_Y;
        uint8_t color = right ? 15 : palette[current_color];
        if (!drawing)
            canvas_brush(cx, cy, color);
        else
            canvas_line(last_cx, last_cy, cx, cy, color);
        last_cx = cx;
        last_cy = cy;
        drawing = 1;
    } else {
        drawing = 0;
    }
    prev_left = left;
}

static void draw_icon(int x, int y, const char *title, uint8_t accent) {
    rect(x, y, MGR_ICON_W, MGR_ICON_H, 15);
    border(x, y, MGR_ICON_W, MGR_ICON_H, 15, 8);
    rect(x + 4, y + 5, 18, 18, accent);
    border(x + 4, y + 5, 18, 18, 15, 0);
    text_clip(x + 28, y + 11, title, 8, 1, -1);
}

static void draw_manager(void) {
    rect(0, TOP_H, SW, SH - TOP_H, 18);
    text(20, 17, "BUILTIN", 15, -1);
    draw_icon(MGR_PAINT_X, MGR_PAINT_Y, "PAINT", 10);
    draw_icon(MGR_SHELL_X, MGR_SHELL_Y, "SHELL", 11);
    draw_icon(MGR_REFRESH_X, MGR_REFRESH_Y, "REFRESH", 14);

    text(20, 73, "/FS/APPS", 15, -1);
    if (app_count == 0) {
        text(20, 91, "EMPTY", 8, -1);
        return;
    }

    for (int i = 0; i < app_count; i++) {
        int col = i % 3;
        int row = i / 3;
        int ax = 20 + col * 98;
        int ay = 86 + row * 28;
        draw_icon(ax, ay, apps[i].name, 13);
    }
}

static void draw_topbar(void) {
    rect(0, 0, SW, TOP_H, 1);
    if (active_app == APP_PAINT)
        text(5, 3, "PAINT", 15, -1);
    else if (active_app == APP_SHELL)
        text(5, 3, "SHELL", 15, -1);
    else
        text(5, 3, "APP MANAGER", 15, -1);

    if (active_app != APP_MANAGER) {
        rect(BACK_X, BACK_Y, BACK_W, BACK_H, 9);
        border(BACK_X, BACK_Y, BACK_W, BACK_H, 15, 0);
        text(BACK_X + 5, BACK_Y + 1, "BACK", 15, -1);
    }
    rect(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, 12);
    border(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, 15, 0);
    text(EXIT_X + 6, EXIT_Y + 1, "EXIT", 15, -1);
}

static void draw_paint(void) {
    rect(PAINT_X, PAINT_Y, PAINT_W, PAINT_H, 7);
    border(PAINT_X, PAINT_Y, PAINT_W, PAINT_H, 15, 0);
    rect(PAINT_X + 1, PAINT_Y + 1, PAINT_W - 2, 11, 9);
    text(PAINT_X + 5, PAINT_Y + 3, "PAINT", 15, -1);

    rect(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_W + 2, CANVAS_H + 2, 0);
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++)
            fb[(CANVAS_Y + y) * SW + CANVAS_X + x] = canvas[y * CANVAS_W + x];
    }

    for (int i = 0; i < (int)(sizeof(palette) / sizeof(palette[0])); i++) {
        int x = CANVAS_X + i * 12;
        rect(x, PALETTE_Y, 10, 10, palette[i]);
        border(x, PALETTE_Y, 10, 10, i == current_color ? 14 : 15, 0);
    }
}

static void draw_shell(void) {
    rect(SHELL_X, SHELL_Y, SHELL_W, SHELL_H, 7);
    border(SHELL_X, SHELL_Y, SHELL_W, SHELL_H, 15, 0);
    rect(SHELL_X + 1, SHELL_Y + 1, SHELL_W - 2, 10, 1);
    text(SHELL_X + 5, SHELL_Y + 3, "SHELL", 15, -1);

    int tx = SHELL_X + 5;
    int ty = SHELL_Y + 14;
    int input_y = SHELL_Y + SHELL_H - 9;
    int max_lines = (input_y - ty - 1) / 8;
    if (max_lines > SHELL_LINES)
        max_lines = SHELL_LINES;
    int start = SHELL_LINES - max_lines;
    for (int i = 0; i < max_lines; i++)
        text(tx, ty + i * 8, shell_lines[start + i], 0, -1);

    rect(SHELL_X + 4, input_y, SHELL_W - 8, 7, 15);
    text(SHELL_X + 7, input_y, ">", 1, -1);
    text_clip(SHELL_X + 15, input_y, shell_input, 48, 1, -1);
    if ((shell_len & 1) == 0) {
        int cx = SHELL_X + 15 + shell_len * 6;
        if (cx < SHELL_X + SHELL_W - 8)
            rect(cx, input_y + 6, 5, 1, 1);
    }
}

static void draw_pointer(void) {
    int x = pointer_x;
    int y = pointer_y;
    static const char *cursor[16] = {
        "X...........",
        "XX..........",
        "XOX.........",
        "XOOX........",
        "XOOOX.......",
        "XOOOOX......",
        "XOOOOOX.....",
        "XOOOOOOX....",
        "XOOOOOOOX...",
        "XOOOOOOOOX..",
        "XOOOOXXXXX..",
        "XOOXOX......",
        "XOX.XOX.....",
        "XX..XOX.....",
        "X....XOX....",
        ".....XXX....",
    };

    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 12; xx++) {
            char c = cursor[yy][xx];
            if (c == 'X')
                px(x + xx, y + yy, 0);
            else if (c == 'O')
                px(x + xx, y + yy, 15);
        }
}

static void render(void) {
    rect(0, 0, SW, SH, 18);
    draw_topbar();
    if (active_app == APP_PAINT)
        draw_paint();
    else if (active_app == APP_SHELL)
        draw_shell();
    else
        draw_manager();
    draw_pointer();
}

static void init_state(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 15;
    for (int i = 0; i < SHELL_LINES; i++)
        shell_lines[i][0] = 0;
    shell_input[0] = 0;
    shell_len = 0;
    shell_log("READY");
    shell_log("HELP LS CAT RUN APPS");
    scan_apps();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (gfx_mode(1) < 0) {
        puts("gui: graphics mode failed");
        return 1;
    }

    init_state();
    while (running) {
        int key;
        while ((key = read_key_poll()) >= 0)
            handle_key(key);
        handle_mouse();
        render();
        fb_blit(0, 0, SW, SH, fb);
        sleep_ms(16);
    }

    gfx_mode(0);
    return 0;
}
