#!/usr/bin/env python3
import argparse
import re
from pathlib import Path
from string import Template

ROOT = Path(__file__).resolve().parents[1]


C_TEMPLATE = Template(r'''#include "libc.h"

#define APP_TITLE "$upper"
#define STATE_PATH "/fs/apps/$name.cfg"

enum {
    KEY_ESC = 0x1B,
    CTRL_C = 0x03,
    SW = 320,
    SH = 200,
    INPUT_MAX = 36,
};

static int running = 1;
static int pointer_x = SW / 2;
static int pointer_y = SH / 2;
static int prev_left;
static int focused = 1;
static int dirty;
static int saved_flash;
static unsigned int frame;
static uint32_t last_mouse_seq;
static char text[INPUT_MAX + 1] = "$upper";

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
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

static void button(int x, int y, int w, const char *label, int hot) {
    int fill = hot ? 30 : 24;
    gfx_fill_rect(x, y, w, 16, fill);
    gfx_fill_rect(x, y, w, 1, 63);
    gfx_fill_rect(x, y + 15, w, 1, 8);
    gfx_text(x + 8, y + 5, label, 15, fill);
}

static void draw(void) {
    int over_save = inside(pointer_x, pointer_y, 22, 142, 54, 16);
    int over_load = inside(pointer_x, pointer_y, 84, 142, 54, 16);
    int over_clear = inside(pointer_x, pointer_y, 146, 142, 60, 16);
    int over_exit = inside(pointer_x, pointer_y, 266, 8, 42, 16);
    int cursor_on = focused && ((frame / 24) & 1);

    gfx_clear(17);
    gfx_fill_rect(0, 0, SW, 24, 21);
    gfx_text(12, 9, APP_TITLE, 15, 21);
    button(266, 8, 42, "EXIT", over_exit);

    gfx_text(22, 44, "Generated BuzzOS GUI app", 15, 17);
    gfx_text(22, 62, "Text field:", 12, 17);
    gfx_fill_rect(22, 78, 276, 24, focused ? 63 : 8);
    gfx_fill_rect(23, 79, 274, 22, 0);
    gfx_text(28, 86, text, 15, 0);
    if (cursor_on) {
        int x = 28 + (int)strlen(text) * 8;
        if (x > 290)
            x = 290;
        gfx_fill_rect(x, 84, 1, 12, 15);
    }

    gfx_text(22, 116, dirty ? "State: unsaved" : "State: clean", dirty ? 14 : 10, 17);
    if (saved_flash > 0)
        gfx_text(142, 116, "saved", 10, 17);

    button(22, 142, 54, "SAVE", over_save);
    button(84, 142, 54, "LOAD", over_load);
    button(146, 142, 60, "CLEAR", over_clear);

    gfx_fill_rect(pointer_x, pointer_y, 6, 1, 15);
    gfx_fill_rect(pointer_x, pointer_y, 1, 6, 15);
}

static void handle_key(char ch) {
    int len;
    if (ch == KEY_ESC || ch == CTRL_C) {
        running = 0;
        return;
    }
    if (!focused)
        return;
    len = (int)strlen(text);
    if (ch == '\r' || ch == '\n') {
        save_state();
    } else if (ch == '\b' || ch == 127) {
        if (len > 0) {
            text[len - 1] = 0;
            mark_dirty();
        }
    } else if (ch >= 32 && ch <= 126 && len < INPUT_MAX) {
        text[len] = ch;
        text[len + 1] = 0;
        mark_dirty();
    }
}

static void handle_mouse(void) {
    struct mouse_state ms;
    if (mouse_get(&ms) < 0 || ms.seq == last_mouse_seq)
        return;
    last_mouse_seq = ms.seq;
    pointer_x = ms.x;
    pointer_y = ms.y;
    if (pointer_x < 0) pointer_x = 0;
    if (pointer_y < 0) pointer_y = 0;
    if (pointer_x >= SW) pointer_x = SW - 1;
    if (pointer_y >= SH) pointer_y = SH - 1;

    int left = ms.buttons & 1;
    if (left && !prev_left) {
        focused = inside(pointer_x, pointer_y, 22, 78, 276, 24);
        if (inside(pointer_x, pointer_y, 22, 142, 54, 16))
            save_state();
        else if (inside(pointer_x, pointer_y, 84, 142, 54, 16))
            load_state();
        else if (inside(pointer_x, pointer_y, 146, 142, 60, 16)) {
            set_text("");
            mark_dirty();
            focused = 1;
        } else if (inside(pointer_x, pointer_y, 266, 8, 42, 16))
            running = 0;
    }
    prev_left = left;
}

int main(void) {
    if (gfx_mode(1) < 0) {
        puts("$name: graphics mode failed");
        exit(1);
    }
    load_state();
    while (running) {
        char ch;
        handle_mouse();
        while (read(0, &ch, 1) == 1)
            handle_key(ch);
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
