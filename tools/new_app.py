#!/usr/bin/env python3
import argparse
import re
from pathlib import Path
from string import Template

ROOT = Path(__file__).resolve().parents[1]


C_TEMPLATE = Template(r'''#include "libc.h"
#include "gui_style.h"

#define APP_TITLE "$upper"
#define STATE_PATH "/fs/apps/$name.cfg"

enum {
    KEY_ESC = 0x1B,
    CTRL_C = 0x03,
    CTRL_S = 0x13,
    KEY_UP = 256,
    KEY_DOWN,
    KEY_DELETE,

    INPUT_MAX = 34,
    LIST_X = 16,
    LIST_Y = 48,
    LIST_W = 112,
    LIST_H = 84,
    ROW_H = 14,

    FIELD_X = 144,
    FIELD_Y = 74,
    FIELD_W = 150,
    FIELD_H = 20,

    SAVE_X = 144,
    LOAD_X = 196,
    CLEAR_X = 248,
    BTN_Y = 128,
    BTN_W = 46,
    BTN_H = 14,
};

static const char *items[] = {
    "OVERVIEW",
    "TEXTBOX",
    "SCROLLBAR",
    "HIGHLIGHT",
    "MOUSE WHEEL",
    "PERSISTENCE",
    "FILES",
    "STATUS",
};

static int running = 1;
static int pointer_x = UI_SW / 2;
static int pointer_y = UI_SH / 2;
static int prev_left;
static int focused = 1;
static int dirty;
static int saved_flash;
static unsigned int frame;
static struct ui_scroll list_scroll;
static char text[INPUT_MAX + 1] = "$upper";

static int item_count(void) {
    return (int)(sizeof(items) / sizeof(items[0]));
}

static int list_visible_rows(void) {
    return ui_visible_rows(LIST_H, ROW_H);
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

static void set_text(const char *s) {
    int i = 0;
    while (s && s[i] && s[i] != '\n' && s[i] != '\r' && i < INPUT_MAX) {
        text[i] = s[i];
        i++;
    }
    text[i] = 0;
}

static void mark_dirty(void) {
    dirty = 1;
    saved_flash = 0;
}

static void save_state(void) {
    int fd = open(STATE_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return;
    write(fd, text, strlen(text));
    write(fd, "\n", 1);
    close(fd);
    dirty = 0;
    saved_flash = 40;
}

static void load_state(void) {
    char buf[INPUT_MAX + 2];
    int fd = open(STATE_PATH, O_RDONLY);
    if (fd < 0)
        return;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;
    set_text(buf);
    dirty = 0;
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
    if (k == 'A')
        return KEY_UP;
    if (k == 'B')
        return KEY_DOWN;
    if (k == '3' && read_byte_poll() == '~')
        return KEY_DELETE;
    return KEY_ESC;
}

static void handle_key(int key) {
    int len;
    if (key == KEY_ESC || key == CTRL_C) {
        running = 0;
        return;
    }
    if (key == CTRL_S) {
        save_state();
        return;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        ui_scroll_select_delta(&list_scroll, key == KEY_UP ? -1 : 1,
                               item_count(), list_visible_rows());
        return;
    }
    if (key == '\t') {
        focused = !focused;
        return;
    }
    if (!focused)
        return;

    len = (int)strlen(text);
    if (key == '\r' || key == '\n') {
        save_state();
    } else if (key == '\b' || key == 127) {
        if (len > 0) {
            text[len - 1] = 0;
            mark_dirty();
        }
    } else if (key == KEY_DELETE) {
        set_text("");
        mark_dirty();
    } else if (key >= 32 && key <= 126 && len < INPUT_MAX) {
        text[len] = (char)key;
        text[len + 1] = 0;
        mark_dirty();
    }
}

static void click_at(int x, int y) {
    if (ui_inside(x, y, UI_EXIT_X, UI_EXIT_Y, UI_EXIT_W, UI_EXIT_H)) {
        running = 0;
        return;
    }
    if (ui_inside(x, y, FIELD_X, FIELD_Y, FIELD_W, FIELD_H)) {
        focused = 1;
        return;
    }
    if (ui_inside(x, y, LIST_X, LIST_Y, LIST_W, LIST_H)) {
        int row = (y - LIST_Y) / ROW_H;
        int selected = list_scroll.first + row;
        if (selected >= 0 && selected < item_count())
            ui_scroll_select(&list_scroll, selected, item_count(), list_visible_rows());
        focused = 0;
        return;
    }
    if (ui_inside(x, y, SAVE_X, BTN_Y, BTN_W, BTN_H)) {
        save_state();
        return;
    }
    if (ui_inside(x, y, LOAD_X, BTN_Y, BTN_W, BTN_H)) {
        load_state();
        return;
    }
    if (ui_inside(x, y, CLEAR_X, BTN_Y, BTN_W, BTN_H)) {
        set_text("");
        mark_dirty();
        focused = 1;
    }
}

static void handle_mouse(void) {
    struct mouse_state ms;
    int wheel;
    int left;
    if (mouse_get(&ms) < 0)
        return;

    pointer_x = ms.x;
    pointer_y = ms.y;

    wheel = ui_mouse_wheel_delta(&ms, &list_scroll);
    if (wheel && ui_inside(pointer_x, pointer_y, LIST_X, LIST_Y, LIST_W, LIST_H)) {
        ui_scroll_select_delta(&list_scroll, ui_wheel_to_rows(wheel),
                               item_count(), list_visible_rows());
    }

    left = ms.buttons & 1;
    if (left && !prev_left)
        click_at(pointer_x, pointer_y);
    prev_left = left;
}

static void draw_list(void) {
    int visible = list_visible_rows();
    gfx_text(LIST_X, LIST_Y - 12, "FEATURES", UI_TEXT, -1);
    for (int row = 0; row < visible; row++) {
        int index = list_scroll.first + row;
        int y = LIST_Y + row * ROW_H;
        if (index >= item_count())
            break;
        ui_list_row(LIST_X, y, LIST_W - 6, ROW_H - 2, items[index],
                    ui_inside(pointer_x, pointer_y, LIST_X, y, LIST_W - 6, ROW_H - 2),
                    index == list_scroll.selected);
    }
    ui_scrollbar(LIST_X + LIST_W - 4, LIST_Y, LIST_H,
                 item_count(), visible, list_scroll.first);
}

static void draw_detail(void) {
    char line[48];
    ui_panel(138, 42, 166, 78, "DETAIL", UI_ACCENT_ALT);
    ui_text_clip(146, 60, items[list_scroll.selected], 23, UI_TEXT, -1);
    line[0] = 0;
    append_text(line, "ROW ", sizeof(line));
    append_uint(line, (unsigned int)(list_scroll.selected + 1), sizeof(line));
    append_text(line, " OF ", sizeof(line));
    append_uint(line, (unsigned int)item_count(), sizeof(line));
    ui_text_clip(146, 73, line, 23, UI_TEXT_DIM, -1);
    ui_text_clip(146, 91, "USE WHEEL OR ARROWS", 23, UI_TEXT, -1);
    ui_text_clip(146, 104, "CLICK ROWS TO SELECT", 23, UI_TEXT, -1);
}

static void draw_status(void) {
    char line[64];
    line[0] = 0;
    append_text(line, dirty ? "DIRTY " : "CLEAN ", sizeof(line));
    append_text(line, STATE_PATH, sizeof(line));
    ui_text_clip(12, 178, line, 49,
                 saved_flash > 0 ? UI_OK : (dirty ? UI_DANGER : UI_ACCENT),
                 -1);
}

static void draw(void) {
    gfx_clear(UI_BG);
    ui_topbar(APP_TITLE,
              ui_inside(pointer_x, pointer_y, UI_EXIT_X, UI_EXIT_Y,
                        UI_EXIT_W, UI_EXIT_H));
    ui_panel(6, 18, 308, 176, "USER GUI APP", UI_ACCENT);

    draw_list();
    draw_detail();

    ui_textbox(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, "TEXT",
               text, "TYPE HERE", 22,
               ui_inside(pointer_x, pointer_y, FIELD_X, FIELD_Y, FIELD_W, FIELD_H),
               focused, (int)strlen(text), frame);
    ui_button(SAVE_X, BTN_Y, BTN_W, BTN_H, "SAVE",
              ui_inside(pointer_x, pointer_y, SAVE_X, BTN_Y, BTN_W, BTN_H), 0);
    ui_button(LOAD_X, BTN_Y, BTN_W, BTN_H, "LOAD",
              ui_inside(pointer_x, pointer_y, LOAD_X, BTN_Y, BTN_W, BTN_H), 0);
    ui_button(CLEAR_X, BTN_Y, BTN_W, BTN_H, "CLR",
              ui_inside(pointer_x, pointer_y, CLEAR_X, BTN_Y, BTN_W, BTN_H), 0);

    draw_status();
    ui_pointer(pointer_x, pointer_y);
}

int main(void) {
    if (gfx_mode(1) < 0) {
        puts("$name: graphics mode failed");
        exit(1);
    }
    load_state();
    while (running) {
        int key;
        handle_mouse();
        while ((key = read_key_poll()) >= 0)
            handle_key(key);
        draw();
        if (saved_flash > 0)
            saved_flash--;
        frame++;
        sleep_ms(16);
    }
    gfx_mode(0);
    return 0;
}
''')


def clean_single_line(value):
    return " ".join(value.split())


def validate_name(name):
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,22}", name):
        raise SystemExit("app name must match [a-z][a-z0-9_]{0,22}")
    return name


def write_file(path, content, force):
    if path.exists() and not force:
        raise SystemExit(f"{path} already exists; use --force to overwrite")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def render_manifest(name, summary):
    return (
        f"name={name.upper()}\n"
        "kind=user-gui\n"
        "version=0.1\n"
        f"summary={summary}\n"
        f"exec=/fs/apps/{name}\n"
        f"state=/fs/apps/{name}.cfg\n"
        f"source=src/user/bin/{name}.c\n"
        f"readme=/fs/apps/{name}.readme\n"
    )


def render_readme(name, summary):
    return (
        f"BuzzOS {name.upper()} user GUI app\n"
        "\n"
        f"{summary}\n"
        "\n"
        f"Executable: /fs/apps/{name}\n"
        f"Run: apps run {name}\n"
        f"State: /fs/apps/{name}.cfg\n"
    )


def print_next_steps(name):
    print("")
    print("Next steps:")
    print(f"1. Add `{name}` to GUI_APP_NAMES in Makefile.")
    print("2. Optionally add default state to `src/user/bin/" + name + ".seed`.")
    print("3. Run `make app-registry`, `make app-check`, then `make verify`.")


def main():
    parser = argparse.ArgumentParser(description="Create a BuzzOS user GUI app scaffold")
    parser.add_argument("name", help="lowercase app name, for example todo")
    parser.add_argument("--summary", default="Generated user GUI app", help="manifest summary")
    parser.add_argument("--force", action="store_true", help="overwrite existing scaffold files")
    parser.add_argument("--dry-run", action="store_true", help="show paths without writing files")
    args = parser.parse_args()

    name = validate_name(args.name)
    summary = clean_single_line(args.summary)
    files = {
        ROOT / f"src/user/bin/{name}.c": C_TEMPLATE.substitute(name=name, upper=name.upper()),
        ROOT / f"src/user/bin/{name}.app": render_manifest(name, summary),
        ROOT / f"src/user/bin/{name}.readme": render_readme(name, summary),
    }

    for path in files:
        print(("would create " if args.dry_run else "create ") + str(path.relative_to(ROOT)))
    if not args.dry_run:
        for path, content in files.items():
            write_file(path, content, args.force)
    print_next_steps(name)


if __name__ == "__main__":
    main()
