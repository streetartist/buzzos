#include <stdint.h>
#include "keyboard.h"
#include "net.h"
#include "task.h"
#include "timer.h"
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
    twin("ls cat touch write rm echo clear ps mem uname uptime sleep reboot ping net wget help\n");
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
static void cmd_touch(const char *p) {
    if (!p[0]) { twin("touch: missing name\n"); return; }
    if (vfs_create(p) < 0) twin("touch: failed\n");
}
static void cmd_write(const char *p) {
    /* write <file> <content> */
    if (!p[0]) { twin("write: usage: write <file> <text>\n"); return; }
    const char *space = p;
    while (*space && *space != ' ') space++;
    if (!*space) { twin("write: missing content\n"); return; }
    /* copy filename */
    char name[32];
    int i = 0;
    while (p < space && i < 31) name[i++] = *p++;
    name[i] = 0;
    const char *content = space + 1;
    int len = 0;
    while (content[len]) len++;
    if (vfs_write_file(name, content, (size_t)len) < 0)
        twin("write: failed\n");
}
static void cmd_rm(const char *p) {
    if (!p[0]) { twin("rm: missing name\n"); return; }
    if (vfs_remove(p) < 0) twin("rm: not found\n");
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
    if (ip == 0 && net_dns_resolve(a, &ip) < 0) {
        twin("bad ip\n"); return;
    }
    int r = net_ping(ip);
    twin(r ? "no reply\n" : "reply ok\n");
}
static void cmd_wget(const char *a) {
    if (!a[0]) { twin("wget: missing host\n"); return; }
    twin("wget "); twin(a); twin("...\n");
    int r = net_wget(a, vga_ser_putc);
    twin(r ? "\nwget: failed\n" : "\nwget: done\n");
}
static void cmd_net(void) { net_status(); }
static void cmd_dhcp(void) {
    twin("requesting IP via DHCP...\n");
    if (net_dhcp() < 0) twin("dhcp: failed\n");
    else { twin("dhcp: ok, IP="); twin("10.0.2.15\n"); }
}

/* Print an unsigned decimal (no printf in the kernel). */
static void twin_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { twin("0"); return; }
    while (v && i < 10) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    char out[12];
    int j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j] = 0;
    twin(out);
}

/* Parse an unsigned decimal from the front of a string. */
static uint32_t parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); s++; }
    return v;
}

static void cmd_uptime(void) {
    uint32_t secs = timer_uptime_secs();
    twin("up ");
    twin_u32(secs / 3600);     twin("h ");
    twin_u32((secs / 60) % 60); twin("m ");
    twin_u32(secs % 60);        twin("s (");
    twin_u32(secs);             twin("s, ");
    twin_u32(timer_ticks()); twin(" ticks)\n");
}

static void cmd_sleep(const char *a) {
    uint32_t secs = parse_u32(a);
    if (secs == 0) { twin("usage: sleep <seconds>\n"); return; }
    twin("sleeping "); twin_u32(secs); twin("s...\n");
    timer_sleep_ms(secs * 1000);
    twin("awake\n");
}

static void execute(const char *cmd) {
    nl();
    if      (starts_with(cmd, "help"))   cmd_help();
    else if (starts_with(cmd, "echo "))  cmd_echo(cmd+5);
    else if (starts_with(cmd, "clear"))  cmd_clear();
    else if (starts_with(cmd, "ls"))     cmd_ls();
    else if (starts_with(cmd, "ps"))     cmd_ps();
    else if (starts_with(cmd, "mem"))    cmd_mem();
    else if (starts_with(cmd, "uname"))  cmd_uname();
    else if (starts_with(cmd, "uptime")) cmd_uptime();
    else if (starts_with(cmd, "sleep "))  cmd_sleep(cmd+6);
    else if (starts_with(cmd, "reboot")) cmd_reboot();
    else if (starts_with(cmd, "cat "))   cmd_cat(cmd+4);
    else if (starts_with(cmd, "touch ")) cmd_touch(cmd+6);
    else if (starts_with(cmd, "write ")) cmd_write(cmd+6);
    else if (starts_with(cmd, "rm "))    cmd_rm(cmd+3);
    else if (starts_with(cmd, "ping "))  cmd_ping(cmd+5);
    else if (starts_with(cmd, "wget "))  cmd_wget(cmd+5);
    else if (starts_with(cmd, "net"))    cmd_net();
    else if (starts_with(cmd, "dhcp"))   cmd_dhcp();
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
