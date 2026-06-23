#include "libc.h"

#define BUF_MAX 4096
#define SCREEN_COLS 80
#define SCREEN_ROWS 25
#define EDIT_TOP 3
#define EDIT_ROWS 22

#define CTRL_C 0x03
#define CTRL_D 0x04
#define CTRL_L 0x0C
#define CTRL_S 0x13
#define CTRL_T 0x14
#define CTRL_X 0x18

enum {
    KEY_UP = 256,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

static char buffer[BUF_MAX];
static int  buffer_len;
static int  cursor;
static int  rowoff;
static int  desired_col = -1;
static int  dirty;
static char status_msg[80];

static const char asm_template[] =
    "bits 32\n"
    "section .text\n"
    "global _start\n"
    "\n"
    "%define SYS_EXIT  1\n"
    "%define SYS_WRITE 5\n"
    "\n"
    "_start:\n"
    "    mov eax, SYS_WRITE\n"
    "    mov ebx, 1\n"
    "    mov ecx, msg\n"
    "    mov edx, msg_len\n"
    "    int 0x80\n"
    "\n"
    "    mov eax, SYS_EXIT\n"
    "    xor ebx, ebx\n"
    "    int 0x80\n"
    "\n"
    "section .rodata\n"
    "msg:\n"
    "    db \"hello from asm\", 10\n"
    "msg_len equ $ - msg\n";

static void set_status(const char *s) {
    int i = 0;
    while (s && s[i] && i < (int)sizeof(status_msg) - 1) {
        status_msg[i] = s[i];
        i++;
    }
    status_msg[i] = 0;
}

static int ends_with(const char *s, const char *suffix) {
    int slen = (int)strlen(s);
    int tlen = (int)strlen(suffix);
    if (tlen > slen)
        return 0;
    return strcmp(s + slen - tlen, suffix) == 0;
}

static void term_uint(int v) {
    char tmp[12];
    int n = 0;
    if (v <= 0) {
        putchar('0');
        return;
    }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        putchar(tmp[--n]);
}

static void term_move(int row, int col) {
    putchar(0x1B);
    putchar('[');
    term_uint(row);
    putchar(';');
    term_uint(col);
    putchar('H');
}

static void term_clear(void) {
    write(1, "\x1B[2J\x1B[H", 7);
}

static void term_clear_line(void) {
    write(1, "\x1B[K", 3);
}

static int read_raw_blocking(void) {
    char c;
    for (;;) {
        int n = read(0, &c, 1);
        if (n > 0)
            return (unsigned char)c;
        yield();
    }
}

static int read_raw_poll(void) {
    char c;
    int n = read(0, &c, 1);
    if (n > 0)
        return (unsigned char)c;
    return -1;
}

static int read_key(void) {
    int c = read_raw_blocking();
    if (c != 0x1B)
        return c;

    int c1 = -1;
    for (int i = 0; i < 20 && c1 < 0; i++) {
        c1 = read_raw_poll();
        if (c1 < 0)
            yield();
    }
    if (c1 != '[')
        return 0x1B;

    int c2 = -1;
    for (int i = 0; i < 20 && c2 < 0; i++) {
        c2 = read_raw_poll();
        if (c2 < 0)
            yield();
    }

    switch (c2) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '3': {
        int c3 = read_raw_poll();
        if (c3 == '~')
            return KEY_DELETE;
        return 0x1B;
    }
    default:
        return 0x1B;
    }
}

static void load_file(const char *path) {
    buffer_len = 0;
    cursor = 0;
    rowoff = 0;
    dirty = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_status("new file");
        return;
    }

    for (;;) {
        int room = BUF_MAX - buffer_len;
        if (room <= 0) {
            set_status("file truncated to editor buffer");
            break;
        }
        int n = read(fd, buffer + buffer_len, (size_t)room);
        if (n <= 0)
            break;
        buffer_len += n;
    }
    close(fd);
    set_status("loaded");
}

static int save_file(const char *path) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) {
        set_status("save failed: open");
        return -1;
    }

    int off = 0;
    while (off < buffer_len) {
        int n = write(fd, buffer + off, (size_t)(buffer_len - off));
        if (n <= 0) {
            close(fd);
            set_status("save failed: write");
            return -1;
        }
        off += n;
    }
    close(fd);
    dirty = 0;
    set_status("saved");
    return 0;
}

static int line_start(int pos) {
    if (pos > buffer_len)
        pos = buffer_len;
    while (pos > 0 && buffer[pos - 1] != '\n')
        pos--;
    return pos;
}

static int line_end(int pos) {
    while (pos < buffer_len && buffer[pos] != '\n')
        pos++;
    return pos;
}

static int line_start_for_line(int target) {
    int line = 0;
    int pos = 0;
    if (target <= 0)
        return 0;
    while (pos < buffer_len && line < target) {
        if (buffer[pos++] == '\n')
            line++;
    }
    return pos;
}

static void cursor_line_col(int pos, int *line_out, int *col_out) {
    int line = 0;
    int col = 0;
    if (pos > buffer_len)
        pos = buffer_len;
    for (int i = 0; i < pos; i++) {
        if (buffer[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    *line_out = line;
    *col_out = col;
}

static void ensure_cursor_visible(void) {
    int line, col;
    cursor_line_col(cursor, &line, &col);
    (void)col;
    if (line < rowoff)
        rowoff = line;
    if (line >= rowoff + EDIT_ROWS)
        rowoff = line - EDIT_ROWS + 1;
    if (rowoff < 0)
        rowoff = 0;
}

static void insert_bytes(const char *s, int n) {
    if (n <= 0)
        return;
    if (buffer_len + n > BUF_MAX) {
        set_status("buffer full");
        return;
    }
    for (int i = buffer_len - 1; i >= cursor; i--)
        buffer[i + n] = buffer[i];
    for (int i = 0; i < n; i++)
        buffer[cursor + i] = s[i];
    cursor += n;
    buffer_len += n;
    dirty = 1;
    desired_col = -1;
}

static void insert_char(char c) {
    insert_bytes(&c, 1);
}

static void delete_before_cursor(void) {
    if (cursor <= 0)
        return;
    for (int i = cursor; i < buffer_len; i++)
        buffer[i - 1] = buffer[i];
    cursor--;
    buffer_len--;
    dirty = 1;
    desired_col = -1;
}

static void delete_at_cursor(void) {
    if (cursor >= buffer_len)
        return;
    for (int i = cursor + 1; i < buffer_len; i++)
        buffer[i - 1] = buffer[i];
    buffer_len--;
    dirty = 1;
    desired_col = -1;
}

static void move_left(void) {
    if (cursor > 0)
        cursor--;
    desired_col = -1;
}

static void move_right(void) {
    if (cursor < buffer_len)
        cursor++;
    desired_col = -1;
}

static void move_home(void) {
    cursor = line_start(cursor);
    desired_col = -1;
}

static void move_end(void) {
    cursor = line_end(cursor);
    desired_col = -1;
}

static void move_vertical(int dir) {
    int line, col;
    cursor_line_col(cursor, &line, &col);
    if (desired_col < 0)
        desired_col = col;

    int target = line + dir;
    if (target < 0)
        return;
    int start = line_start_for_line(target);
    if (target > 0 && start >= buffer_len && (buffer_len == 0 || buffer[buffer_len - 1] != '\n'))
        return;

    int end = line_end(start);
    int len = end - start;
    cursor = start + (desired_col < len ? desired_col : len);
}

static void render_line(int line_no, int screen_row) {
    term_move(screen_row, 1);
    term_clear_line();

    int start = line_start_for_line(line_no);
    if (start >= buffer_len && !(line_no == 0 && buffer_len == 0)) {
        putchar('~');
        return;
    }

    int col = 0;
    for (int i = start; i < buffer_len && buffer[i] != '\n' && col < SCREEN_COLS; i++) {
        char c = buffer[i];
        if (c == '\t') {
            int spaces = 4 - (col % 4);
            while (spaces-- && col < SCREEN_COLS) {
                putchar(' ');
                col++;
            }
        } else if (c >= 32 && c < 127) {
            putchar(c);
            col++;
        } else {
            putchar('?');
            col++;
        }
    }
}

static void redraw(const char *path) {
    ensure_cursor_visible();
    term_clear();

    term_move(1, 1);
    term_clear_line();
    printf("BuzzOS nano  %s%s", path, dirty ? " *" : "");

    term_move(2, 1);
    term_clear_line();
    puts("^S save  ^C quit  ^X quit  ^T asm template  arrows move");

    for (int i = 0; i < EDIT_ROWS; i++)
        render_line(rowoff + i, EDIT_TOP + i);

    term_move(SCREEN_ROWS, 1);
    term_clear_line();
    printf("%s", status_msg);

    int line, col;
    cursor_line_col(cursor, &line, &col);
    int screen_row = EDIT_TOP + (line - rowoff);
    int screen_col = col + 1;
    if (screen_col > SCREEN_COLS)
        screen_col = SCREEN_COLS;
    if (screen_row < EDIT_TOP)
        screen_row = EDIT_TOP;
    if (screen_row >= SCREEN_ROWS)
        screen_row = SCREEN_ROWS - 1;
    term_move(screen_row, screen_col);
}

static int confirm_quit(void) {
    if (!dirty)
        return 1;
    set_status("unsaved changes; press Ctrl+C again to discard");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: nano <file>");
        puts("example: nano /fs/demo.asm");
        return 1;
    }

    const char *path = argv[1];
    load_file(path);
    if (buffer_len == 0 && ends_with(path, ".asm"))
        set_status("Ctrl+T inserts a minimal BuzzOS assembly template");

    int quit_pending = 0;
    for (;;) {
        redraw(path);
        int c = read_key();

        if (c == CTRL_S) {
            save_file(path);
            quit_pending = 0;
        } else if (c == CTRL_L) {
            quit_pending = 0;
        } else if (c == CTRL_T) {
            insert_bytes(asm_template, (int)strlen(asm_template));
            set_status("inserted asm template");
            quit_pending = 0;
        } else if (c == CTRL_C || c == CTRL_X || c == CTRL_D) {
            if (quit_pending || confirm_quit()) {
                term_clear();
                return 0;
            }
            quit_pending = 1;
        } else {
            quit_pending = 0;
            switch (c) {
            case KEY_UP: move_vertical(-1); break;
            case KEY_DOWN: move_vertical(1); break;
            case KEY_RIGHT: move_right(); break;
            case KEY_LEFT: move_left(); break;
            case KEY_HOME: move_home(); break;
            case KEY_END: move_end(); break;
            case KEY_DELETE: delete_at_cursor(); break;
            case '\r':
            case '\n': insert_char('\n'); break;
            case '\b':
            case 0x7F: delete_before_cursor(); break;
            case '\t': insert_bytes("    ", 4); break;
            default:
                if (c >= 32 && c < 127)
                    insert_char((char)c);
                break;
            }
        }
    }
}
