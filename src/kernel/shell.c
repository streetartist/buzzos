#include <stdint.h>
#include "keyboard.h"
#include "net.h"
#include "task.h"
#include "vfs.h"
#include "vga.h"

#define MAX_CMD 128
static char cmd_buf[MAX_CMD];
static int  cmd_len, out_fd = -1;

static void ser_putc(char c) { vfs_write(out_fd, &c, 1); }
static void vga_ser_putc(char c) { ser_putc(c); vga_putc(c); }
static void twin(const char *s) { while (*s) vga_ser_putc(*s++); }
static void prompt(void) { twin("\nbuzzos> "); }
static void nl(void) { twin("\n"); }
static int starts_with(const char *a, const char *p) {
    while (*p) if (*a++ != *p++) return 0; return 1;
}

static void cmd_help(void) {
    twin("ls cat echo clear ps mem uname reboot ping net help\n");
}
static void cmd_echo(const char *a) { twin(a[0]?a:"(empty)"); }
static void cmd_clear(void) { vga_clear(); }
static void cmd_ls(void) { vfs_ls(vga_ser_putc); }
static void cmd_ps(void) { twin("PID  STATE    NAME\n  0  RUNNING  idle\n  1  RUNNING  shell\n"); }
static void cmd_mem(void) { twin("128 MiB RAM\n"); }
static void cmd_uname(void) { twin("BuzzOS i686\n"); }
static void cmd_reboot(void) { twin("rebooting...\n"); __asm__ volatile("int $0"); }
static void cmd_cat(const char *p) {
    if (!p[0]) { twin("cat: missing file\n"); return; }
    if (vfs_cat(p, vga_ser_putc) < 0) twin("cat: not found\n"); else nl();
}
static uint32_t parse_ip(const char *s) {
    uint32_t ip = 0;
    int part = 0, num = 0;
    for (;; s++) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
        } else if (c == '.' || c == 0 || c == ' ' || c == '\n' || c == '\r') {
            if (num > 255 || part > 3) return 0;
            ip |= (uint32_t)num << (part * 8);
            part++;
            num = 0;
            if (c != '.') break;
        } else {
            return 0; /* invalid char */
        }
    }
    return (part == 4) ? ip : 0;
}

static void cmd_ping(const char *a) {
    twin("ping "); twin(a); twin("...\n");
    uint32_t ip = parse_ip(a);
    if (ip == 0) { twin("bad ip\n"); return; }
    int r = net_ping(ip);
    twin(r ? "no reply\n" : "reply ok\n");
}
static void cmd_net(void) { net_status(); }

static void execute(const char *cmd) {
    nl();
    if      (starts_with(cmd, "help"))   cmd_help();
    else if (starts_with(cmd, "echo "))  cmd_echo(cmd+5);
    else if (starts_with(cmd, "clear"))  cmd_clear();
    else if (starts_with(cmd, "ls"))     cmd_ls();
    else if (starts_with(cmd, "ps"))     cmd_ps();
    else if (starts_with(cmd, "mem"))    cmd_mem();
    else if (starts_with(cmd, "uname"))  cmd_uname();
    else if (starts_with(cmd, "reboot")) cmd_reboot();
    else if (starts_with(cmd, "cat "))   cmd_cat(cmd+4);
    else if (starts_with(cmd, "ping "))  cmd_ping(cmd+5);
    else if (starts_with(cmd, "net"))    cmd_net();
    else twin("? try help\n");
}

void shell_task(void) {
    out_fd = vfs_open("/dev/serial");
    if (out_fd < 0) return;
    twin("\n=== BuzzOS Shell ===\n");
    vga_clear(); vga_puts("BuzzOS Shell\n"); vga_puts("type help\n\n");
    prompt();
    for (;;) {
        int c = keyboard_getchar();
        if (c < 0) { task_yield(); continue; }
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_len] = 0;
            if (cmd_len > 0) execute(cmd_buf);
            cmd_len = 0; prompt();
        } else if (c == '\b' || c == 0x7F) {
            if (cmd_len > 0) { cmd_len--; ser_putc('\b'); ser_putc(' '); ser_putc('\b'); vga_backspace(); }
        } else if (c >= 32 && c < 127 && cmd_len < MAX_CMD-1) {
            cmd_buf[cmd_len++] = (char)c;
            ser_putc((char)c); vga_putc((char)c);
        }
    }
}
