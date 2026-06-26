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
    STATUS_H = 11,

    APP_MANAGER = 0,
    APP_PAINT = 1,
    APP_SHELL = 2,
    APP_HELP = 3,

    SCROLL_FOCUS_LIST = 0,
    SCROLL_FOCUS_DETAIL = 1,

    BACK_X = 246,
    BACK_Y = 2,
    BACK_W = 34,
    BACK_H = 8,
    EXIT_X = 286,
    EXIT_Y = 2,
    EXIT_W = 28,
    EXIT_H = 8,

    MGR_ICON_W = 54,
    MGR_ICON_H = 13,
    MGR_PAINT_X = 10,
    MGR_PAINT_Y = 31,
    MGR_SHELL_X = 70,
    MGR_SHELL_Y = 31,
    MGR_HELP_X = 130,
    MGR_HELP_Y = 31,

    PAINT_X = 4,
    PAINT_Y = 15,
    PAINT_W = 312,
    PAINT_H = 181,
    CANVAS_X = 8,
    CANVAS_Y = 29,
    CANVAS_W = 198,
    CANVAS_H = 98,
    PALETTE_Y = 132,
    PAINT_PANEL_X = 216,
    PAINT_PANEL_Y = 29,
    PAINT_PANEL_W = 96,
    PAINT_PANEL_H = 145,
    PAINT_BRUSH_X = 218,
    PAINT_ERASE_X = 267,
    PAINT_MODE_Y = 86,
    PAINT_MODE_W = 43,
    PAINT_MODE_H = 12,
    PAINT_CLEAR_X = 218,
    PAINT_CLEAR_Y = 105,
    PAINT_CLEAR_W = 92,
    PAINT_CLEAR_H = 12,
    PAINT_STATUS_Y = 180,
    PAINT_STATUS_H = 12,

    SHELL_X = 4,
    SHELL_Y = 15,
    SHELL_W = 312,
    SHELL_H = 181,
    SHELL_HELP_X = 220,
    SHELL_APPS_X = 251,
    SHELL_CLEAR_X = 282,
    SHELL_BTN_Y = SHELL_Y + 2,
    SHELL_BTN_W = 28,
    SHELL_CLEAR_W = 32,
    SHELL_BTN_H = 8,

    SHELL_COLS = 50,
    SHELL_LINES = 18,
    SHELL_HISTORY_MAX = 8,

    HELP_X = 4,
    HELP_Y = 15,
    HELP_W = 312,
    HELP_H = 181,
    HELP_RUN_X = 218,
    HELP_RUN_Y = 34,
    HELP_RUN_W = 88,
    HELP_RUN_H = 12,

    APPS_X = 4,
    APPS_Y = 15,
    APPS_W = 312,
    APPS_H = 181,
    APPS_LIST_X = 10,
    APPS_LIST_Y = 62,
    APPS_LIST_W = 124,
    APPS_LIST_H = 100,
    APPS_ROW_H = 16,
    APPS_DETAIL_X = 144,
    APPS_DETAIL_Y = 50,
    APPS_DETAIL_W = 166,
    APPS_DETAIL_H = 112,
    APPS_DETAIL_LINE_H = 12,
    APPS_DETAIL_VISIBLE_ROWS = 7,
    APPS_DETAIL_MAX_LINES = 12,
    APPS_RUN_X = 146,
    APPS_RUN_Y = 164,
    APPS_RUN_W = 52,
    APPS_SCAN_X = 202,
    APPS_SCAN_Y = 164,
    APPS_SCAN_W = 58,
    APPS_BTN_H = 12,
    APPS_STATUS_Y = 181,

    APP_MAX = 10,
};

struct app_entry {
    char name[24];
    char path[64];
    uint32_t size;
};

struct app_meta {
    char name[24];
    char kind[18];
    char version[12];
    char summary[48];
    char state[64];
    char source[64];
    char readme[64];
};

static uint8_t fb[SW * SH];
static uint8_t canvas[CANVAS_W * CANVAS_H];
static struct app_entry apps[APP_MAX];
static int app_count;

static char shell_lines[SHELL_LINES][SHELL_COLS + 1];
static char shell_input[SHELL_COLS + 1];
static char shell_history[SHELL_HISTORY_MAX][SHELL_COLS + 1];
static int shell_len;
static int shell_history_count;
static int shell_history_pos;
static int app_selected;
static int app_detail_scroll;
static int app_scroll_focus = SCROLL_FOCUS_LIST;

static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int current_color = 0;
static int paint_erase;
static int running = 1;
static int active_app = APP_MANAGER;
static int prev_left;
static int drawing;
static int last_cx;
static int last_cy;
static unsigned int frame_tick;
static uint32_t last_mouse_seq;
static int last_wheel;
static uint32_t last_wheel_seq;

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

static void clear_canvas(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 15;
}

static int is_elf_file(const char *path) {
    uint8_t magic[4];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    int n = read(fd, magic, sizeof(magic));
    close(fd);
    return n == 4 && magic[0] == 0x7F && magic[1] == 'E' &&
        magic[2] == 'L' && magic[3] == 'F';
}

static int manifest_key_len(const char *line, int line_len, const char *key) {
    int i = 0;
    while (key[i]) {
        if (i >= line_len || line[i] != key[i])
            return 0;
        i++;
    }
    return i < line_len && line[i] == '=' ? i : 0;
}

static int copy_manifest_value(char *dst, int cap, const char *line,
                               int line_len, const char *key) {
    int key_len = manifest_key_len(line, line_len, key);
    if (!key_len)
        return 0;
    int src = key_len + 1;
    int out = 0;
    while (src < line_len && out < cap - 1) {
        dst[out++] = line[src++];
    }
    dst[out] = 0;
    return 1;
}

static void parse_manifest_line(struct app_meta *meta,
                                const char *line,
                                int line_len) {
    if (copy_manifest_value(meta->name, sizeof(meta->name), line, line_len, "name"))
        return;
    if (copy_manifest_value(meta->kind, sizeof(meta->kind), line, line_len, "kind"))
        return;
    if (copy_manifest_value(meta->version, sizeof(meta->version), line, line_len, "version"))
        return;
    if (copy_manifest_value(meta->summary, sizeof(meta->summary), line, line_len, "summary"))
        return;
    if (copy_manifest_value(meta->state, sizeof(meta->state), line, line_len, "state"))
        return;
    if (copy_manifest_value(meta->source, sizeof(meta->source), line, line_len, "source"))
        return;
    (void)copy_manifest_value(meta->readme, sizeof(meta->readme), line, line_len, "readme");
}

static void load_app_manifest(struct app_meta *meta, const struct app_entry *app) {
    char manifest_path[80];
    char buf[384];

    copy_text(meta->name, app->name, sizeof(meta->name));
    copy_text(meta->kind, "user-elf", sizeof(meta->kind));
    copy_text(meta->version, "-", sizeof(meta->version));
    copy_text(meta->summary, "RUNS OUTSIDE BUILTINS", sizeof(meta->summary));
    copy_text(meta->state, "-", sizeof(meta->state));
    copy_text(meta->source, "-", sizeof(meta->source));
    copy_text(meta->readme, "-", sizeof(meta->readme));

    manifest_path[0] = 0;
    append_text(manifest_path, app->path, sizeof(manifest_path));
    append_text(manifest_path, ".app", sizeof(manifest_path));

    int fd = open(manifest_path, O_RDONLY);
    if (fd < 0)
        return;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;

    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            int len = i - start;
            if (len > 0 && buf[start + len - 1] == '\r')
                len--;
            if (len > 0)
                parse_manifest_line(meta, buf + start, len);
            start = i + 1;
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
            char path[64];
            copy_text(path, "/fs/apps/", sizeof(path));
            append_text(path, ents[i].d_name, sizeof(path));
            if (!is_elf_file(path))
                continue;
            copy_text(apps[app_count].name, ents[i].d_name, sizeof(apps[app_count].name));
            copy_text(apps[app_count].path, path, sizeof(apps[app_count].path));
            struct stat st;
            apps[app_count].size = 0;
            if (stat(apps[app_count].path, &st) == 0)
                apps[app_count].size = st.st_size;
            app_count++;
        }
    }
    close(fd);
    if (app_selected >= app_count)
        app_selected = app_count > 0 ? app_count - 1 : 0;
    if (app_selected < 0)
        app_selected = 0;
    app_detail_scroll = 0;
    app_scroll_focus = SCROLL_FOCUS_LIST;
}

static int launch_external(const char *path) {
    int status = -1;
    gfx_mode(0);
    char *argv[1];
    argv[0] = (char *)path;
    int pid = spawn_process_args(path, argv, 1, 0);
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

static void shell_cmd_pwd(void) {
    char cwd[64];
    if (getcwd(cwd, sizeof(cwd)))
        shell_log(cwd);
    else
        shell_log("PWD: FAILED");
}

static void shell_cmd_stat(const char *path) {
    path = skip_spaces(path);
    if (!path[0]) {
        shell_log("STAT: PATH");
        return;
    }

    struct stat st;
    if (stat(path, &st) < 0) {
        shell_log("STAT: NOT FOUND");
        return;
    }

    char line[SHELL_COLS + 1];
    line[0] = 0;
    if ((st.st_mode & S_IFMT) == S_IFDIR)
        append_text(line, "DIR ", sizeof(line));
    else if ((st.st_mode & S_IFMT) == S_IFREG)
        append_text(line, "FILE ", sizeof(line));
    else if ((st.st_mode & S_IFMT) == S_IFCHR)
        append_text(line, "CHAR ", sizeof(line));
    else
        append_text(line, "NODE ", sizeof(line));
    append_text(line, "SIZE ", sizeof(line));
    append_uint(line, st.st_size, sizeof(line));
    shell_log(line);
}

static void shell_cmd_write(const char *args) {
    args = skip_spaces(args);
    char path[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && i < (int)sizeof(path) - 1) {
        path[i] = args[i];
        i++;
    }
    path[i] = 0;
    args = skip_spaces(args + i);
    if (!path[0] || !args[0]) {
        shell_log("WRITE: PATH TEXT");
        return;
    }

    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) {
        shell_log("WRITE: OPEN FAILED");
        return;
    }
    int n = write(fd, args, strlen(args));
    close(fd);
    shell_log(n < 0 ? "WRITE: FAILED" : "WRITE: OK");
}

static void shell_cmd_rm(const char *path) {
    path = skip_spaces(path);
    if (!path[0]) {
        shell_log("RM: PATH");
        return;
    }
    shell_log(unlink(path) < 0 ? "RM: FAILED" : "RM: OK");
}

static void shell_cmd_ps(void) {
    char buf[512];
    if (ps(buf, sizeof(buf), 0) < 0) {
        shell_log("PS: FAILED");
        return;
    }
    shell_log(buf);
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

static void shell_print_help_topic(const char *topic) {
    topic = skip_spaces(topic);
    if (!topic[0]) {
        shell_log("HELP TOPICS: APPS GUI FILES");
        shell_log("PROC EDIT  TRY ABOUT");
        shell_log("RUN: GUIDEMO NOTES FORMS CALC");
        shell_log("CMDS: ABOUT HEALTH LIMITS FSINFO");
        return;
    }
    if (streq(topic, "apps")) {
        shell_log("APPS LISTS /FS/APPS");
        shell_log("RUN GUIDEMO NOTES FORMS CALC");
        shell_log("DETAILS ON APP MANAGER");
        shell_log("README BESIDE EACH APP");
        return;
    }
    if (streq(topic, "gui")) {
        shell_log("MANAGER: 1 PAINT 2 SHELL");
        shell_log("3 HELP 4/ENTER RUN APP");
        shell_log("ESC BACK, CTRL-C EXIT");
        shell_log("TEXTBOXES USE CLICK/TAB");
        return;
    }
    if (streq(topic, "files")) {
        shell_log("/FS IS WRITABLE MINIFS");
        shell_log("WRITE /FS/A TEXT; CAT /FS/A");
        shell_log("RM MV MKDIR RMDIR STAT");
        shell_log("FSINFO SHOWS /PROC/FS");
        return;
    }
    if (streq(topic, "proc")) {
        shell_log("/PROC IS READ-ONLY");
        shell_log("ABOUT = /PROC/ABOUT");
        shell_log("HEALTH = /PROC/HEALTH");
        shell_log("LIMITS = /PROC/LIMITS");
        shell_log("INTERFACES = /PROC/INTERFACES");
        shell_log("FSINFO = /PROC/FS");
        shell_log("CAT /PROC/ABOUT");
        shell_log("CAT /PROC/LIMITS");
        shell_log("CAT /PROC/HEALTH");
        shell_log("CAT /PROC/INTERFACES");
        shell_log("CAT /PROC/FS TASKS NET FDS");
        return;
    }
    if (streq(topic, "edit")) {
        shell_log("SHELL: ARROWS HOME END DEL");
        shell_log("UP/DOWN HISTORY BACKSPACE");
        shell_log("GUI: CLICK/TAB FOCUS");
        shell_log("ENTER ACCEPTS TEXT");
        return;
    }
    shell_log("HELP: UNKNOWN TOPIC");
}

static void shell_print_help(void) {
    shell_print_help_topic("");
}

static void shell_history_add(const char *cmd) {
    if (!cmd || !cmd[0])
        return;
    if (shell_history_count > 0 &&
        streq(shell_history[shell_history_count - 1], cmd)) {
        shell_history_pos = shell_history_count;
        return;
    }

    if (shell_history_count == SHELL_HISTORY_MAX) {
        for (int i = 1; i < SHELL_HISTORY_MAX; i++)
            copy_text(shell_history[i - 1], shell_history[i], sizeof(shell_history[i - 1]));
        shell_history_count--;
    }
    copy_text(shell_history[shell_history_count], cmd, sizeof(shell_history[shell_history_count]));
    shell_history_count++;
    shell_history_pos = shell_history_count;
}

static void shell_history_browse(int dir) {
    if (shell_history_count == 0)
        return;

    if (dir < 0) {
        if (shell_history_pos > 0)
            shell_history_pos--;
        else
            return;
    } else {
        if (shell_history_pos < shell_history_count - 1)
            shell_history_pos++;
        else {
            shell_history_pos = shell_history_count;
            shell_input[0] = 0;
            shell_len = 0;
            return;
        }
    }

    copy_text(shell_input, shell_history[shell_history_pos], sizeof(shell_input));
    shell_len = (int)strlen(shell_input);
}

static void shell_execute(void) {
    const char *cmd = skip_spaces(shell_input);
    if (!cmd[0])
        return;
    shell_history_add(cmd);

    char echo[SHELL_COLS + 1];
    echo[0] = 0;
    append_text(echo, "> ", sizeof(echo));
    append_text(echo, cmd, sizeof(echo));
    shell_log(echo);

    if (streq(cmd, "help") || starts_with(cmd, "help ")) {
        shell_print_help_topic(cmd + 4);
    } else if (streq(cmd, "clear")) {
        for (int i = 0; i < SHELL_LINES; i++)
            shell_lines[i][0] = 0;
    } else if (streq(cmd, "apps")) {
        shell_list_apps();
    } else if (streq(cmd, "pwd")) {
        shell_cmd_pwd();
    } else if (streq(cmd, "about")) {
        shell_cmd_cat("/proc/about");
    } else if (streq(cmd, "health")) {
        shell_cmd_cat("/proc/health");
    } else if (streq(cmd, "limits")) {
        shell_cmd_cat("/proc/limits");
    } else if (streq(cmd, "fsinfo")) {
        shell_cmd_cat("/proc/fs");
    } else if (streq(cmd, "interfaces")) {
        shell_cmd_cat("/proc/interfaces");
    } else if (streq(cmd, "ps") || starts_with(cmd, "ps ")) {
        shell_cmd_ps();
    } else if (streq(cmd, "ls") || starts_with(cmd, "ls ")) {
        shell_cmd_ls(cmd + 2);
    } else if (starts_with(cmd, "cat ")) {
        shell_cmd_cat(cmd + 4);
    } else if (starts_with(cmd, "stat ")) {
        shell_cmd_stat(cmd + 5);
    } else if (starts_with(cmd, "echo ")) {
        shell_log(cmd + 5);
    } else if (starts_with(cmd, "write ")) {
        shell_cmd_write(cmd + 6);
    } else if (starts_with(cmd, "rm ")) {
        shell_cmd_rm(cmd + 3);
    } else if (streq(cmd, "guidemo") || streq(cmd, "demo")) {
        shell_cmd_run("/fs/apps/guidemo");
    } else if (streq(cmd, "notes")) {
        shell_cmd_run("/fs/apps/notes");
    } else if (streq(cmd, "forms")) {
        shell_cmd_run("/fs/apps/forms");
    } else if (streq(cmd, "calc")) {
        shell_cmd_run("/fs/apps/calc");
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

static int app_visible_rows(void) {
    return APPS_LIST_H / APPS_ROW_H;
}

static int app_list_start(void) {
    int visible = app_visible_rows();
    int start = 0;
    if (app_selected >= visible)
        start = app_selected - visible + 1;
    if (start > app_count - visible)
        start = app_count - visible;
    if (start < 0)
        start = 0;
    return start;
}

static void select_app(int selected) {
    if (app_count <= 0) {
        app_selected = 0;
        app_detail_scroll = 0;
        return;
    }
    if (selected < 0)
        selected = 0;
    if (selected >= app_count)
        selected = app_count - 1;
    if (selected != app_selected)
        app_detail_scroll = 0;
    app_selected = selected;
}

static void scroll_app_list(int delta) {
    select_app(app_selected + delta);
}

static void add_detail_line(char (*lines)[48], int *count,
                            int max_lines, const char *line) {
    if (lines && *count < max_lines)
        copy_text(lines[*count], line, 48);
    (*count)++;
}

static void add_detail_value(char (*lines)[48], int *count,
                             int max_lines, const char *label,
                             const char *value) {
    char line[64];
    line[0] = 0;
    append_text(line, label, sizeof(line));
    append_text(line, value && value[0] ? value : "-", sizeof(line));
    add_detail_line(lines, count, max_lines, line);
}

static int build_app_detail_lines(const struct app_entry *app,
                                  char (*lines)[48],
                                  int max_lines) {
    struct app_meta meta;
    char line[64];
    int count = 0;
    if (!app)
        return 0;

    load_app_manifest(&meta, app);
    add_detail_line(lines, &count, max_lines, meta.name);
    add_detail_line(lines, &count, max_lines, meta.summary);
    add_detail_value(lines, &count, max_lines, "PATH ", app->path);
    add_detail_value(lines, &count, max_lines, "KIND ", meta.kind);
    add_detail_value(lines, &count, max_lines, "VERSION ", meta.version);

    line[0] = 0;
    append_text(line, "SIZE ", sizeof(line));
    append_uint(line, app->size, sizeof(line));
    append_text(line, " B", sizeof(line));
    add_detail_line(lines, &count, max_lines, line);

    add_detail_value(lines, &count, max_lines, "STATE ", meta.state);
    add_detail_value(lines, &count, max_lines, "SOURCE ", meta.source);
    add_detail_value(lines, &count, max_lines, "README ", meta.readme);
    return count;
}

static int app_detail_line_count(void) {
    if (app_count <= 0)
        return 0;
    return build_app_detail_lines(&apps[app_selected], 0, 0);
}

static int app_detail_max_scroll(void) {
    int max_scroll = app_detail_line_count() - APPS_DETAIL_VISIBLE_ROWS;
    if (max_scroll < 0)
        max_scroll = 0;
    return max_scroll;
}

static void clamp_app_detail_scroll(void) {
    int max_scroll = app_detail_max_scroll();
    if (app_detail_scroll < 0)
        app_detail_scroll = 0;
    if (app_detail_scroll > max_scroll)
        app_detail_scroll = max_scroll;
}

static void scroll_app_detail(int delta) {
    app_detail_scroll += delta;
    clamp_app_detail_scroll();
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
    if (active_app == APP_SHELL && (k == KEY_UP || k == KEY_DOWN)) {
        shell_history_browse(k == KEY_UP ? -1 : 1);
        return;
    }
    if (k == KEY_LEFT) {
        if (active_app == APP_MANAGER) {
            app_scroll_focus = SCROLL_FOCUS_DETAIL;
            scroll_app_detail(-1);
            return;
        }
        if (pointer_x > 0) pointer_x -= 4;
        return;
    }
    if (k == KEY_RIGHT) {
        if (active_app == APP_MANAGER) {
            app_scroll_focus = SCROLL_FOCUS_DETAIL;
            scroll_app_detail(1);
            return;
        }
        if (pointer_x < SW - 1) pointer_x += 4;
        return;
    }
    if (k == KEY_UP) {
        if (active_app == APP_MANAGER) {
            app_scroll_focus = SCROLL_FOCUS_LIST;
            scroll_app_list(-1);
            return;
        }
        if (pointer_y > TOP_H) pointer_y -= 4;
        return;
    }
    if (k == KEY_DOWN) {
        if (active_app == APP_MANAGER) {
            app_scroll_focus = SCROLL_FOCUS_LIST;
            scroll_app_list(1);
            return;
        }
        if (pointer_y < SH - 1) pointer_y += 4;
        return;
    }
    if (active_app == APP_MANAGER) {
        if (k == '\r')
            k = '\n';
        if (k == '1' || k == 'p' || k == 'P')
            active_app = APP_PAINT;
        else if (k == '2' || k == 's' || k == 'S')
            active_app = APP_SHELL;
        else if (k == '3' || k == 'h' || k == 'H')
            active_app = APP_HELP;
        else if (k == '4' || k == 'a' || k == 'A' ||
                 k == '\n' || k == 'd' || k == 'D' || k == 'g' || k == 'G')
            shell_cmd_run(app_count > 0 ? apps[app_selected].path : "/fs/apps/guidemo");
        else if (k == 'r' || k == 'R')
            scan_apps();
        return;
    }
    if (active_app == APP_PAINT) {
        if (k == 'c' || k == 'C')
            clear_canvas();
        else if (k == 'e' || k == 'E')
            paint_erase = 1;
        else if (k == 'b' || k == 'B')
            paint_erase = 0;
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
        shell_history_pos = shell_history_count;
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

static int in_app_list(int x, int y) {
    return inside(x, y, APPS_LIST_X - 2, APPS_LIST_Y - 2,
                  APPS_LIST_W + 4, APPS_LIST_H + 4);
}

static int in_app_detail(int x, int y) {
    return inside(x, y, APPS_DETAIL_X, APPS_DETAIL_Y,
                  APPS_DETAIL_W, APPS_DETAIL_H);
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
        if (inside(x, y, MGR_HELP_X, MGR_HELP_Y, MGR_ICON_W, MGR_ICON_H)) {
            active_app = APP_HELP;
            return;
        }
        int start = app_list_start();
        int visible = app_visible_rows();
        if (in_app_list(x, y))
            app_scroll_focus = SCROLL_FOCUS_LIST;
        for (int row = 0; row < visible && start + row < app_count; row++) {
            int i = start + row;
            int row_y = APPS_LIST_Y + row * APPS_ROW_H;
            if (inside(x, y, APPS_LIST_X, row_y, APPS_LIST_W, APPS_ROW_H - 2)) {
                app_scroll_focus = SCROLL_FOCUS_LIST;
                select_app(i);
                return;
            }
        }
        if (in_app_detail(x, y)) {
            app_scroll_focus = SCROLL_FOCUS_DETAIL;
            return;
        }
        if (inside(x, y, APPS_RUN_X, APPS_RUN_Y, APPS_RUN_W, APPS_BTN_H)) {
            if (app_count > 0)
                shell_cmd_run(apps[app_selected].path);
            return;
        }
        if (inside(x, y, APPS_SCAN_X, APPS_SCAN_Y, APPS_SCAN_W, APPS_BTN_H)) {
            scan_apps();
            return;
        }
        return;
    }

    if (active_app == APP_HELP) {
        if (inside(x, y, HELP_RUN_X, HELP_RUN_Y, HELP_RUN_W, HELP_RUN_H))
            shell_cmd_run("/fs/apps/guidemo");
        return;
    }

    if (active_app == APP_SHELL) {
        if (inside(x, y, SHELL_HELP_X, SHELL_BTN_Y, SHELL_BTN_W, SHELL_BTN_H)) {
            shell_print_help();
            return;
        }
        if (inside(x, y, SHELL_APPS_X, SHELL_BTN_Y, SHELL_BTN_W, SHELL_BTN_H)) {
            shell_list_apps();
            return;
        }
        if (inside(x, y, SHELL_CLEAR_X, SHELL_BTN_Y, SHELL_CLEAR_W, SHELL_BTN_H)) {
            for (int i = 0; i < SHELL_LINES; i++)
                shell_lines[i][0] = 0;
            return;
        }
    }

    if (active_app == APP_PAINT) {
        if (inside(x, y, PAINT_BRUSH_X, PAINT_MODE_Y, PAINT_MODE_W, PAINT_MODE_H)) {
            paint_erase = 0;
            return;
        }
        if (inside(x, y, PAINT_ERASE_X, PAINT_MODE_Y, PAINT_MODE_W, PAINT_MODE_H)) {
            paint_erase = 1;
            return;
        }
        if (inside(x, y, PAINT_CLEAR_X, PAINT_CLEAR_Y, PAINT_CLEAR_W, PAINT_CLEAR_H)) {
            clear_canvas();
            return;
        }
        for (int i = 0; i < (int)(sizeof(palette) / sizeof(palette[0])); i++) {
            int px0 = CANVAS_X + i * 12;
            if (inside(x, y, px0, PALETTE_Y, 10, 10)) {
                current_color = i;
                paint_erase = 0;
                return;
            }
        }
    }
}

static void handle_mouse_wheel(int wheel) {
    int delta;
    int steps;
    int target;
    if (active_app != APP_MANAGER || wheel == 0)
        return;

    delta = wheel > 0 ? -1 : 1;
    steps = wheel > 0 ? wheel : -wheel;
    if (steps > 8)
        steps = 8;

    target = app_scroll_focus;
    if (in_app_list(pointer_x, pointer_y))
        target = SCROLL_FOCUS_LIST;
    else if (in_app_detail(pointer_x, pointer_y))
        target = SCROLL_FOCUS_DETAIL;

    app_scroll_focus = target;
    if (target == SCROLL_FOCUS_LIST) {
        for (int i = 0; i < steps; i++)
            scroll_app_list(delta);
        return;
    }
    if (target == SCROLL_FOCUS_DETAIL) {
        for (int i = 0; i < steps; i++)
            scroll_app_detail(delta);
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
        if (m.wheel_seq != last_wheel_seq) {
            int wheel = m.wheel - last_wheel;
            last_wheel = m.wheel;
            last_wheel_seq = m.wheel_seq;
            handle_mouse_wheel(wheel);
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
        uint8_t color = (right || paint_erase) ? 15 : palette[current_color];
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

static void draw_button(int x, int y, int w, int h, const char *label, int active) {
    int hot = inside(pointer_x, pointer_y, x, y, w, h);
    uint8_t fill = active ? 9 : (hot ? 15 : 7);
    uint8_t fg = active ? 15 : 1;
    rect(x, y, w, h, fill);
    border(x, y, w, h, hot ? 14 : 15, active ? 0 : 8);

    int text_w = (int)strlen(label) * 6;
    int tx = x + (w - text_w) / 2;
    if (tx < x + 2)
        tx = x + 2;
    text(tx, y + (h - 7) / 2, label, fg, -1);
}

static void draw_scrollbar(int x, int y, int h,
                           int total, int visible, int first) {
    int thumb_h;
    int thumb_y;
    int max_first = total - visible;
    if (total <= visible || h <= 0 || max_first <= 0)
        return;
    rect(x, y, 3, h, 8);
    thumb_h = (h * visible) / total;
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > h)
        thumb_h = h;
    thumb_y = y + ((h - thumb_h) * first) / max_first;
    rect(x, thumb_y, 3, thumb_h, 1);
}

static void draw_topbar(void) {
    rect(0, 0, SW, TOP_H, 1);
    rect(0, TOP_H - 1, SW, 1, 8);
    if (active_app == APP_PAINT)
        text(5, 3, "PAINT", 15, -1);
    else if (active_app == APP_SHELL)
        text(5, 3, "SHELL", 15, -1);
    else if (active_app == APP_HELP)
        text(5, 3, "HELP", 15, -1);
    else
        text(5, 3, "APP MANAGER", 15, -1);

    if (active_app != APP_MANAGER) {
        int hot = inside(pointer_x, pointer_y, BACK_X, BACK_Y, BACK_W, BACK_H);
        rect(BACK_X, BACK_Y, BACK_W, BACK_H, hot ? 14 : 9);
        border(BACK_X, BACK_Y, BACK_W, BACK_H, 15, 0);
        text(BACK_X + 5, BACK_Y + 1, "BACK", hot ? 1 : 15, -1);
    }
    int hot = inside(pointer_x, pointer_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H);
    rect(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, hot ? 14 : 12);
    border(EXIT_X, EXIT_Y, EXIT_W, EXIT_H, 15, 0);
    text(EXIT_X + 6, EXIT_Y + 1, "EXIT", hot ? 1 : 15, -1);
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

    rect(PAINT_PANEL_X, PAINT_PANEL_Y, PAINT_PANEL_W, PAINT_PANEL_H, 15);
    border(PAINT_PANEL_X, PAINT_PANEL_Y, PAINT_PANEL_W, PAINT_PANEL_H, 15, 8);
    text(PAINT_PANEL_X + 6, PAINT_PANEL_Y + 6, "COLOR", 1, -1);
    rect(PAINT_PANEL_X + 6, PAINT_PANEL_Y + 19, 28, 16, palette[current_color]);
    border(PAINT_PANEL_X + 6, PAINT_PANEL_Y + 19, 28, 16, 15, 0);

    char color_line[24];
    color_line[0] = 0;
    append_text(color_line, "IDX ", sizeof(color_line));
    append_uint(color_line, (unsigned int)current_color, sizeof(color_line));
    text(PAINT_PANEL_X + 40, PAINT_PANEL_Y + 24, color_line, 1, -1);

    text(PAINT_PANEL_X + 6, PAINT_PANEL_Y + 48, "MODE", 1, -1);
    draw_button(PAINT_BRUSH_X, PAINT_MODE_Y, PAINT_MODE_W, PAINT_MODE_H, "DRAW", !paint_erase);
    draw_button(PAINT_ERASE_X, PAINT_MODE_Y, PAINT_MODE_W, PAINT_MODE_H, "ERASE", paint_erase);
    draw_button(PAINT_CLEAR_X, PAINT_CLEAR_Y, PAINT_CLEAR_W, PAINT_CLEAR_H, "CLEAR", 0);

    for (int i = 0; i < (int)(sizeof(palette) / sizeof(palette[0])); i++) {
        int x = CANVAS_X + i * 12;
        rect(x, PALETTE_Y, 10, 10, palette[i]);
        border(x, PALETTE_Y, 10, 10, i == current_color ? 14 : 15, 0);
    }
    text(CANVAS_X, PALETTE_Y + 13, "PALETTE", 0, -1);

    char status[48];
    status[0] = 0;
    if (inside(pointer_x, pointer_y, CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H)) {
        append_text(status, "X=", sizeof(status));
        append_uint(status, (unsigned int)(pointer_x - CANVAS_X), sizeof(status));
        append_text(status, " Y=", sizeof(status));
        append_uint(status, (unsigned int)(pointer_y - CANVAS_Y), sizeof(status));
    } else {
        append_text(status, paint_erase ? "ERASE" : "DRAW", sizeof(status));
        append_text(status, "  COLOR ", sizeof(status));
        append_uint(status, (unsigned int)current_color, sizeof(status));
    }
    rect(PAINT_X + 4, PAINT_STATUS_Y, PAINT_W - 8, PAINT_STATUS_H, 1);
    text_clip(PAINT_X + 8, PAINT_STATUS_Y + 2, status, 48, 15, -1);
}

static void draw_help(void) {
    rect(HELP_X, HELP_Y, HELP_W, HELP_H, 7);
    border(HELP_X, HELP_Y, HELP_W, HELP_H, 15, 0);
    rect(HELP_X + 1, HELP_Y + 1, HELP_W - 2, 10, 9);
    text(HELP_X + 5, HELP_Y + 3, "HELP", 15, -1);

    text(14, 34, "BUZZOS GUI", 1, -1);
    draw_button(HELP_RUN_X, HELP_RUN_Y, HELP_RUN_W, HELP_RUN_H, "RUN DEMO", 0);

    text(14, 50, "APP MANAGER", 1, -1);
    text(14, 62, "4 OR ENTER RUNS SELECTED APP", 0, -1);
    text(14, 73, "DETAIL SHOWS STATE AND README", 0, -1);
    text(14, 84, "UP DOWN SELECT /FS/APPS", 0, -1);

    text(14, 99, "TEXT INPUT", 1, -1);
    text(14, 111, "CLICK OR TAB FOCUS", 0, -1);
    text(14, 122, "BACKSPACE DELETE HOME END", 0, -1);
    text(14, 133, "ENTER ACCEPTS OR SUBMITS", 0, -1);

    text(14, 148, "SHELL HELP", 1, -1);
    text(14, 160, "ABOUT SHOWS PROJECT INFO", 0, -1);
    text(14, 171, "FSINFO SHOWS /FS STATUS", 0, -1);
    text(14, 182, "LIMITS SHOW CAPACITY BOUNDS", 0, -1);
}

static void draw_manager(void) {
    rect(APPS_X, APPS_Y, APPS_W, APPS_H, 7);
    border(APPS_X, APPS_Y, APPS_W, APPS_H, 15, 0);
    rect(APPS_X + 1, APPS_Y + 1, APPS_W - 2, 10, 13);
    text(APPS_X + 5, APPS_Y + 3, "APP MANAGER", 15, -1);

    draw_button(MGR_PAINT_X, MGR_PAINT_Y, MGR_ICON_W, MGR_ICON_H, "PAINT", 0);
    draw_button(MGR_SHELL_X, MGR_SHELL_Y, MGR_ICON_W, MGR_ICON_H, "SHELL", 0);
    draw_button(MGR_HELP_X, MGR_HELP_Y, MGR_ICON_W, MGR_ICON_H, "HELP", 0);

    text(APPS_LIST_X, APPS_LIST_Y - 12, "/FS/APPS", 1, -1);
    rect(APPS_LIST_X - 2, APPS_LIST_Y - 2, APPS_LIST_W + 4, APPS_LIST_H + 4, 15);
    border(APPS_LIST_X - 2, APPS_LIST_Y - 2, APPS_LIST_W + 4, APPS_LIST_H + 4,
           app_scroll_focus == SCROLL_FOCUS_LIST ? 14 : 15, 8);

    if (app_count == 0) {
        text(APPS_LIST_X + 5, APPS_LIST_Y + 8, "EMPTY", 8, -1);
    } else {
        int start = app_list_start();
        int visible = app_visible_rows();
        for (int row = 0; row < visible && start + row < app_count; row++) {
            int i = start + row;
            int y = APPS_LIST_Y + row * APPS_ROW_H;
            int selected = i == app_selected;
            rect(APPS_LIST_X, y, APPS_LIST_W, APPS_ROW_H - 2, selected ? 9 : 7);
            border(APPS_LIST_X, y, APPS_LIST_W, APPS_ROW_H - 2,
                   selected ? 14 : 15, selected ? 0 : 8);
            rect(APPS_LIST_X + 4, y + 4, 10, 8, 13);
            text_clip(APPS_LIST_X + 20, y + 5, apps[i].name, 14,
                      selected ? 15 : 1, -1);
        }
        draw_scrollbar(APPS_LIST_X + APPS_LIST_W - 5, APPS_LIST_Y,
                       APPS_LIST_H, app_count, visible, start);
    }

    rect(APPS_DETAIL_X, APPS_DETAIL_Y, APPS_DETAIL_W, APPS_DETAIL_H, 15);
    border(APPS_DETAIL_X, APPS_DETAIL_Y, APPS_DETAIL_W, APPS_DETAIL_H,
           app_scroll_focus == SCROLL_FOCUS_DETAIL ? 14 : 15, 8);
    text(APPS_DETAIL_X + 6, APPS_DETAIL_Y + 6, "DETAIL", 1, -1);

    if (app_count == 0) {
        text(APPS_DETAIL_X + 6, APPS_DETAIL_Y + 24, "NO APPS FOUND", 8, -1);
        text(APPS_DETAIL_X + 6, APPS_DETAIL_Y + 36, "SEED FAILED OR EMPTY FS", 8, -1);
    } else {
        struct app_entry *app = &apps[app_selected];
        char lines[APPS_DETAIL_MAX_LINES][48];
        int line_count;

        clamp_app_detail_scroll();
        line_count = build_app_detail_lines(app, lines, APPS_DETAIL_MAX_LINES);
        for (int row = 0; row < APPS_DETAIL_VISIBLE_ROWS; row++) {
            int i = app_detail_scroll + row;
            uint8_t color = 0;
            if (i >= line_count)
                break;
            if (i == 0)
                color = 1;
            else if (i == 1)
                color = 13;
            text_clip(APPS_DETAIL_X + 6,
                      APPS_DETAIL_Y + 22 + row * APPS_DETAIL_LINE_H,
                      lines[i], 24, color, -1);
        }
        draw_scrollbar(APPS_DETAIL_X + APPS_DETAIL_W - 5,
                       APPS_DETAIL_Y + 20, APPS_DETAIL_H - 25,
                       line_count, APPS_DETAIL_VISIBLE_ROWS,
                       app_detail_scroll);
    }

    draw_button(APPS_RUN_X, APPS_RUN_Y, APPS_RUN_W, APPS_BTN_H, "RUN", 0);
    draw_button(APPS_SCAN_X, APPS_SCAN_Y, APPS_SCAN_W, APPS_BTN_H, "SCAN", 0);

    rect(APPS_X + 4, APPS_STATUS_Y, APPS_W - 8, 12, 1);
    if (app_count == 0) {
        text(APPS_X + 8, APPS_STATUS_Y + 2, "NO APPS IN /FS/APPS", 15, -1);
    } else {
        char status[64];
        status[0] = 0;
        append_text(status, "SELECTED ", sizeof(status));
        append_uint(status, (unsigned int)(app_selected + 1), sizeof(status));
        append_text(status, "/", sizeof(status));
        append_uint(status, (unsigned int)app_count, sizeof(status));
        if (app_scroll_focus == SCROLL_FOCUS_DETAIL) {
            append_text(status, " DETAIL ", sizeof(status));
            append_uint(status, (unsigned int)(app_detail_scroll + 1), sizeof(status));
            append_text(status, "/", sizeof(status));
            append_uint(status, (unsigned int)(app_detail_max_scroll() + 1), sizeof(status));
        } else {
            append_text(status, " LIST", sizeof(status));
        }
        append_text(status, " ENTER RUN R SCAN", sizeof(status));
        text_clip(APPS_X + 8, APPS_STATUS_Y + 2, status, 50, 15, -1);
    }
}

static void draw_shell(void) {
    rect(SHELL_X, SHELL_Y, SHELL_W, SHELL_H, 7);
    border(SHELL_X, SHELL_Y, SHELL_W, SHELL_H, 15, 0);
    rect(SHELL_X + 1, SHELL_Y + 1, SHELL_W - 2, 10, 1);
    text(SHELL_X + 5, SHELL_Y + 3, "SHELL", 15, -1);
    draw_button(SHELL_HELP_X, SHELL_BTN_Y, SHELL_BTN_W, SHELL_BTN_H, "HELP", 0);
    draw_button(SHELL_APPS_X, SHELL_BTN_Y, SHELL_BTN_W, SHELL_BTN_H, "APPS", 0);
    draw_button(SHELL_CLEAR_X, SHELL_BTN_Y, SHELL_CLEAR_W, SHELL_BTN_H, "CLR", 0);

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
    if (((frame_tick / 20) & 1) == 0) {
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
    frame_tick++;
    rect(0, 0, SW, SH, 18);
    draw_topbar();
    if (active_app == APP_PAINT)
        draw_paint();
    else if (active_app == APP_SHELL)
        draw_shell();
    else if (active_app == APP_HELP)
        draw_help();
    else
        draw_manager();
    draw_pointer();
}

static void init_state(void) {
    clear_canvas();
    for (int i = 0; i < SHELL_LINES; i++)
        shell_lines[i][0] = 0;
    shell_input[0] = 0;
    shell_len = 0;
    shell_history_count = 0;
    shell_history_pos = 0;
    app_selected = 0;
    paint_erase = 0;
    frame_tick = 0;
    shell_log("READY");
    shell_log("ABOUT HEALTH LIMITS FSINFO");
    shell_log("GUIDEMO NOTES FORMS");
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
