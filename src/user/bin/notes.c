#include "libc.h"
#include "gui_style.h"

#define CTRL_C 0x03
#define CTRL_S 0x13
#define CTRL_L 0x0C
#define NOTE_PATH "/fs/apps/notes.txt"

enum {
    SW = 320,
    SH = 200,
    TOP_H = 12,

    EXIT_X = 286,
    EXIT_Y = 2,
    EXIT_W = 28,
    EXIT_H = 8,

    EDIT_X = 10,
    EDIT_Y = 42,
    EDIT_W = 300,
    EDIT_H = 104,
    EDIT_COLS = 48,
    EDIT_ROWS = 10,
    NOTE_MAX = 480,

    SAVE_X = 10,
    LOAD_X = 70,
    CLEAR_X = 130,
    BTN_Y = 154,
    BTN_W = 52,
    BTN_H = 14,
};

static char note[NOTE_MAX + 1];
static int note_len;
static int focused = 1;
static int dirty;
static int flash;
static int running = 1;
static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int prev_left;
static unsigned int frame;
static uint32_t last_mouse_seq;

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void set_status_saved(void) {
    dirty = 0;
    flash = 80;
}

static void set_dirty(void) {
    dirty = 1;
    flash = 0;
}

static void note_clear(void) {
    note_len = 0;
    note[0] = 0;
    set_dirty();
}

static void note_load(void) {
    int fd = open(NOTE_PATH, O_RDONLY);
    if (fd < 0) {
        note_clear();
        dirty = 0;
        return;
    }
    int n = read(fd, note, NOTE_MAX);
    close(fd);
    if (n < 0)
        n = 0;
    note_len = n;
    note[note_len] = 0;
    dirty = 0;
    flash = 40;
}

static void note_save(void) {
    int fd = open(NOTE_PATH, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0)
        return;
    int n = write(fd, note, (size_t)note_len);
    close(fd);
    if (n >= 0)
        set_status_saved();
}

static int read_key_poll(void) {
    unsigned char c;
    int n = read(0, &c, 1);
    if (n > 0)
        return c;
    return -1;
}

static void insert_char(char ch) {
    if (note_len >= NOTE_MAX)
        return;
    note[note_len++] = ch;
    note[note_len] = 0;
    set_dirty();
}

static void backspace(void) {
    if (note_len <= 0)
        return;
    note[--note_len] = 0;
    set_dirty();
}

static void handle_key(int key) {
    if (key == CTRL_C || key == 0x1B) {
        running = 0;
        return;
    }
    if (key == CTRL_S) {
        note_save();
        return;
    }
    if (key == CTRL_L) {
        note_load();
        return;
    }
    if (!focused)
        return;
    if (key == '\r')
        key = '\n';
    if (key == '\b' || key == 0x7F) {
        backspace();
        return;
    }
    if (key == '\n') {
        insert_char('\n');
        return;
    }
    if (key >= 32 && key < 127)
        insert_char((char)key);
}

static void click_at(int x, int y) {
    if (inside(x, y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        running = 0;
        return;
    }
    if (inside(x, y, SAVE_X, BTN_Y, BTN_W, BTN_H)) {
        note_save();
        return;
    }
    if (inside(x, y, LOAD_X, BTN_Y, BTN_W, BTN_H)) {
        note_load();
        return;
    }
    if (inside(x, y, CLEAR_X, BTN_Y, BTN_W, BTN_H)) {
        note_clear();
        return;
    }
    focused = inside(x, y, EDIT_X, EDIT_Y, EDIT_W, EDIT_H);
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

static void draw_note_text(void) {
    int row = 0;
    int col = 0;
    char ch[2];
    ch[1] = 0;

    for (int i = 0; i < note_len && row < EDIT_ROWS; i++) {
        char c = note[i];
        if (c == '\n') {
            row++;
            col = 0;
            continue;
        }
        if (col >= EDIT_COLS) {
            row++;
            col = 0;
        }
        if (row >= EDIT_ROWS)
            break;
        ch[0] = c;
        gfx_text(EDIT_X + 5 + col * 6, EDIT_Y + 6 + row * 9, ch, 1, -1);
        col++;
    }

    if (focused && ((frame / 20u) & 1u) == 0u && row < EDIT_ROWS) {
        int cx = EDIT_X + 5 + col * 6;
        int cy = EDIT_Y + 5 + row * 9;
        if (cx > EDIT_X + EDIT_W - 5)
            cx = EDIT_X + EDIT_W - 5;
        gfx_fill_rect(cx, cy, 1, 8, 1);
    }
}

static void draw_status(void) {
    char line[64];
    int n = 0;
    line[0] = 0;
    const char *status = flash > 0 ? "SAVED" : (dirty ? "DIRTY" : "LOADED");
    while (status[n]) {
        line[n] = status[n];
        n++;
    }
    line[n++] = ' ';
    line[n++] = ' ';
    const char *path = NOTE_PATH;
    for (int i = 0; path[i] && n < (int)sizeof(line) - 1; i++)
        line[n++] = path[i];
    line[n] = 0;
    gfx_text(10, 178, line, flash > 0 ? 10 : (dirty ? 12 : 9), -1);
}

static void draw(void) {
    gfx_clear(UI_BG);
    ui_topbar("NOTES",
              inside(pointer_x, pointer_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H));
    ui_panel(6, 18, 308, 176, "MULTILINE TEXT INPUT", UI_ACCENT_ALT);

    gfx_text(10, 33, "EDITOR", 1, -1);
    ui_field(EDIT_X, EDIT_Y, EDIT_W, EDIT_H,
             inside(pointer_x, pointer_y, EDIT_X, EDIT_Y, EDIT_W, EDIT_H),
             focused);
    draw_note_text();

    ui_button(SAVE_X, BTN_Y, BTN_W, BTN_H, "SAVE",
              inside(pointer_x, pointer_y, SAVE_X, BTN_Y, BTN_W, BTN_H), 0);
    ui_button(LOAD_X, BTN_Y, BTN_W, BTN_H, "LOAD",
              inside(pointer_x, pointer_y, LOAD_X, BTN_Y, BTN_W, BTN_H), 0);
    ui_button(CLEAR_X, BTN_Y, BTN_W, BTN_H, "CLEAR",
              inside(pointer_x, pointer_y, CLEAR_X, BTN_Y, BTN_W, BTN_H), 0);
    gfx_text(194, 158, "CTRL-S SAVE", 1, -1);
    gfx_text(194, 169, "ESC EXIT", 1, -1);
    draw_status();
    ui_pointer(pointer_x, pointer_y);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (gfx_mode(1) < 0) {
        puts("notes: graphics mode failed");
        return 1;
    }
    note_load();
    while (running) {
        int key;
        while ((key = read_key_poll()) >= 0)
            handle_key(key);
        handle_mouse();
        draw();
        if (flash > 0)
            flash--;
        frame++;
        sleep_ms(16);
    }
    gfx_mode(0);
    return 0;
}
