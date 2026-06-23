#include "libc.h"

#define LINE_MAX 128
#define HISTORY_MAX 8

#define CTRL_C 0x03

enum {
    KEY_UP = 256,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

static int futex_demo_word;
static char history[HISTORY_MAX][LINE_MAX];
static int history_len;

static int starts_with(const char *s, const char *p) {
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static int parse_u32(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

static int parse_ipv4(const char *s, uint32_t *out) {
    uint32_t parts[4];
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9')
            return -1;
        uint32_t v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (uint32_t)(*s - '0');
            if (v > 255)
                return -1;
            s++;
        }
        parts[i] = v;
        if (i < 3) {
            if (*s != '.')
                return -1;
            s++;
        }
    }
    if (*s)
        return -1;
    *out = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
    return 0;
}

static void print_ipv4(uint32_t ip) {
    printf("%u.%u.%u.%u",
           ip & 0xFFu, (ip >> 8) & 0xFFu, (ip >> 16) & 0xFFu, (ip >> 24) & 0xFFu);
}

static void trim_right(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = 0;
}

static int split_args(char *s, char *argv[], int max_args) {
    int argc = 0;
    while (*s && argc < max_args) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        argv[argc++] = s;
        while (*s && *s != ' ')
            s++;
        if (*s)
            *s++ = 0;
    }
    return argc;
}

static void prompt(void) {
    char cwd[128];
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");
    printf("\nbuzzos:%s> ", cwd);
}

static int read_raw_blocking(void) {
    unsigned char c;
    for (;;) {
        int n = read(0, &c, 1);
        if (n > 0)
            return c;
        yield();
    }
}

static int read_raw_poll(void) {
    unsigned char c;
    int n = read(0, &c, 1);
    if (n > 0)
        return c;
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

static void term_left(int count) {
    while (count-- > 0)
        write(1, "\x1B[D", 3);
}

static void term_right(int count) {
    while (count-- > 0)
        write(1, "\x1B[C", 3);
}

static void redraw_from(char *line, int len, int from, int target, int clear_extra) {
    for (int i = from; i < len; i++)
        putchar(line[i]);
    if (clear_extra)
        putchar(' ');

    int printed = len - from + (clear_extra ? 1 : 0);
    int keep = target - from;
    if (printed > keep)
        term_left(printed - keep);
}

static void replace_input_line(char *line, int *len, int *cursor, const char *src) {
    int old_len = *len;
    term_left(*cursor);

    int n = 0;
    while (src[n] && n < LINE_MAX - 1) {
        line[n] = src[n];
        n++;
    }
    line[n] = 0;
    *len = n;
    *cursor = n;

    for (int i = 0; i < n; i++)
        putchar(line[i]);
    for (int i = n; i < old_len; i++)
        putchar(' ');
    if (old_len > n)
        term_left(old_len - n);
}

static void history_add(const char *line) {
    if (!line[0])
        return;
    if (history_len > 0 && strcmp(history[history_len - 1], line) == 0)
        return;

    if (history_len == HISTORY_MAX) {
        for (int i = 1; i < HISTORY_MAX; i++)
            strcpy(history[i - 1], history[i]);
        history_len--;
    }
    strcpy(history[history_len++], line);
}

static int read_line(char *line, int size) {
    int len = 0;
    int cursor = 0;
    int hist_index = history_len;
    for (;;) {
        int c = read_key();
        if (c == '\r')
            c = '\n';
        if (c == CTRL_C) {
            term_right(len - cursor);
            puts("^C");
            line[0] = 0;
            return 0;
        }
        if (c == '\n') {
            term_right(len - cursor);
            putchar('\n');
            line[len] = 0;
            return len;
        }
        if (c == KEY_LEFT) {
            if (cursor > 0) {
                cursor--;
                term_left(1);
            }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cursor < len) {
                cursor++;
                term_right(1);
            }
            continue;
        }
        if (c == KEY_HOME) {
            term_left(cursor);
            cursor = 0;
            continue;
        }
        if (c == KEY_END) {
            term_right(len - cursor);
            cursor = len;
            continue;
        }
        if (c == KEY_UP) {
            if (history_len > 0 && hist_index > 0) {
                hist_index--;
                replace_input_line(line, &len, &cursor, history[hist_index]);
            }
            continue;
        }
        if (c == KEY_DOWN) {
            if (hist_index < history_len - 1) {
                hist_index++;
                replace_input_line(line, &len, &cursor, history[hist_index]);
            } else if (hist_index < history_len) {
                hist_index = history_len;
                replace_input_line(line, &len, &cursor, "");
            }
            continue;
        }
        if (c == '\b' || c == 0x7F) {
            if (cursor > 0) {
                for (int i = cursor; i < len; i++)
                    line[i - 1] = line[i];
                len--;
                cursor--;
                term_left(1);
                redraw_from(line, len, cursor, cursor, 1);
            }
            continue;
        }
        if (c == KEY_DELETE) {
            if (cursor < len) {
                for (int i = cursor + 1; i < len; i++)
                    line[i - 1] = line[i];
                len--;
                redraw_from(line, len, cursor, cursor, 1);
            }
            continue;
        }
        if (c >= 32 && c < 127 && len < size - 1) {
            for (int i = len; i > cursor; i--)
                line[i] = line[i - 1];
            line[cursor] = (char)c;
            len++;
            redraw_from(line, len, cursor, cursor + 1, 0);
            cursor++;
        }
    }
}

static void cmd_help(void) {
    puts("ls cd pwd stat cat mkdir rmdir touch write rm mv nano basm ping wget dhcp pipetest futextest exec wait kill ps echo sleep reboot help");
}

static void cmd_ls(const char *path) {
    int fd = open(path[0] ? path : ".", O_RDONLY);
    if (fd < 0) {
        puts("ls: not found");
        return;
    }

    struct dirent ents[8];
    int n;
    while ((n = getdents(fd, ents, sizeof(ents))) > 0) {
        int count = n / (int)sizeof(struct dirent);
        for (int i = 0; i < count; i++) {
            printf("%s", ents[i].d_name);
            if (ents[i].d_type == DT_DIR)
                putchar('/');
            putchar('\n');
        }
    }
    close(fd);
}

static void cmd_pwd(void) {
    char cwd[128];
    if (getcwd(cwd, sizeof(cwd)))
        puts(cwd);
    else
        puts("pwd: failed");
}

static void cmd_stat(const char *path) {
    struct stat st;
    if (!path[0] || stat(path, &st) < 0) {
        puts("stat: not found");
        return;
    }
    if ((st.st_mode & S_IFMT) == S_IFDIR)
        printf("dir size %u\n", st.st_size);
    else if ((st.st_mode & S_IFMT) == S_IFREG)
        printf("file size %u\n", st.st_size);
    else if ((st.st_mode & S_IFMT) == S_IFCHR)
        printf("char size %u\n", st.st_size);
    else
        printf("unknown size %u\n", st.st_size);
}

static void cmd_cat(const char *path) {
    char buf[128];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        puts("cat: not found");
        return;
    }
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, (size_t)n);
    close(fd);
    putchar('\n');
}

static void cmd_write_file(const char *args) {
    char path[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof(path) - 1) {
        path[i] = args[i];
        i++;
    }
    path[i] = 0;
    while (args[i] == ' ')
        i++;
    if (!path[0] || !args[i]) {
        puts("write: usage: write <file> <text>");
        return;
    }
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) {
        puts("write: open failed");
        return;
    }
    if (write(fd, args + i, strlen(args + i)) < 0)
        puts("write: failed");
    close(fd);
}

static void cmd_exec(const char *args) {
    char local[128];
    int n = 0;
    while (args[n] && n < (int)sizeof(local) - 1) {
        local[n] = args[n];
        n++;
    }
    local[n] = 0;
    trim_right(local);

    char *argv[16];
    int argc = split_args(local, argv, 16);

    if (argc == 0) {
        puts("exec: missing path");
        return;
    }

    int bg = 0;
    if (argc > 1 && strcmp(argv[argc - 1], "&") == 0) {
        bg = 1;
        argc--;
    } else if (argc > 1 && strcmp(argv[argc - 1], "bg") == 0) {
        bg = 1;
        argc--;
    }

    int pid = spawn_process_args(argv[0], argv, argc, bg ? 1 : 0);
    if (pid < 0) {
        puts("exec: failed");
        return;
    }
    if (bg) {
        printf("[exec] started task %d\n", pid);
        return;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("[exec] exited %d\n", status);
}

static void cmd_nano(const char *path) {
    while (*path == ' ')
        path++;
    if (!path[0]) {
        puts("nano: usage: nano <file>");
        return;
    }

    char *argv[2];
    argv[0] = "/bin/nano";
    argv[1] = (char *)path;
    int pid = spawn_process_args("/bin/nano", argv, 2, 0);
    if (pid < 0) {
        puts("nano: failed");
        return;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("[nano] exited %d\n", status);
}

static void cmd_basm(const char *args) {
    char local[128];
    int n = 0;
    while (args[n] && n < (int)sizeof(local) - 1) {
        local[n] = args[n];
        n++;
    }
    local[n] = 0;
    trim_right(local);

    char *argsv[2];
    int argn = split_args(local, argsv, 2);
    if (argn < 1) {
        puts("basm: usage: basm <input.asm> [output]");
        return;
    }

    char *argv[3];
    argv[0] = "/bin/basm";
    argv[1] = argsv[0];
    if (argn >= 2)
        argv[2] = argsv[1];

    int pid = spawn_process_args("/bin/basm", argv, argn + 1, 0);
    if (pid < 0) {
        puts("basm: failed");
        return;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("[basm] exited %d\n", status);
}

static void cmd_wait(const char *arg) {
    int pid = arg[0] ? parse_u32(arg) : -1;
    int status = 0;
    int got = waitpid(pid == 0 ? -1 : pid, &status, 0);
    if (got < 0) {
        puts("wait: no child");
        return;
    }
    printf("wait: pid %d exited %d\n", got, status);
}

static void cmd_ps(const char *arg) {
    char buf[1024];
    int show_dead = starts_with(arg, "-a");
    if (ps(buf, sizeof(buf), show_dead) < 0) {
        puts("ps: failed");
        return;
    }
    printf("%s", buf);
}

static void cmd_sockget(const char *host) {
    if (!host[0]) {
        puts("sockget: usage: sockget <host>");
        return;
    }

    uint32_t ip;
    if (dns_resolve(host, &ip) < 0) {
        puts("sockget: dns failed");
        return;
    }

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        puts("sockget: socket failed");
        return;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr = ip;
    if (connect(sd, &addr, sizeof(addr)) < 0) {
        puts("sockget: connect failed");
        closesocket(sd);
        return;
    }

    char req[256];
    int pos = 0;
    const char *a = "GET / HTTP/1.0\r\nHost: ";
    while (*a && pos < (int)sizeof(req)) req[pos++] = *a++;
    const char *h = host;
    while (*h && pos < (int)sizeof(req)) req[pos++] = *h++;
    const char *b = "\r\nUser-Agent: BuzzOS-socket/1.0\r\nConnection: close\r\n\r\n";
    while (*b && pos < (int)sizeof(req)) req[pos++] = *b++;

    if (send(sd, req, (size_t)pos, 0) < 0) {
        puts("sockget: send failed");
        closesocket(sd);
        return;
    }

    char buf[512];
    for (;;) {
        int n = recv(sd, buf, sizeof(buf), 0);
        if (n < 0) {
            puts("sockget: recv failed");
            break;
        }
        if (n == 0)
            break;
        write(1, buf, (size_t)n);
    }
    closesocket(sd);
}

static void cmd_ping(const char *target) {
    uint32_t ip;
    if (parse_ipv4(target, &ip) < 0) {
        if (dns_resolve(target, &ip) < 0) {
            puts("ping: resolve failed");
            return;
        }
    }

    int sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sd < 0) {
        puts("ping: socket failed");
        return;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr = ip;
    if (connect(sd, &addr, sizeof(addr)) < 0) {
        puts("ping: connect failed");
        closesocket(sd);
        return;
    }
    const char payload[] = "buzzos";
    if (send(sd, payload, sizeof(payload), 0) < 0) {
        puts("ping: send failed");
        closesocket(sd);
        return;
    }
    char buf[64];
    int n = recv(sd, buf, sizeof(buf), 0);
    if (n < 0) {
        puts("ping: timeout");
    } else {
        printf("reply from ");
        print_ipv4(ip);
        printf(": %d bytes\n", n);
    }
    closesocket(sd);
}

struct dhcp_msg {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint8_t magic[4];
    uint8_t opts[64];
} __attribute__((packed));

static int dhcp_add_opt(uint8_t *opts, int pos, uint8_t code, const void *data, int len) {
    opts[pos++] = code;
    opts[pos++] = (uint8_t)len;
    const uint8_t *p = (const uint8_t *)data;
    for (int i = 0; i < len; i++)
        opts[pos++] = p[i];
    return pos;
}

static void cmd_dhcp(void) {
    int sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sd < 0) {
        puts("dhcp: socket failed");
        return;
    }

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_port = htons(68);
    local.sin_addr = 0;
    if (bind(sd, &local, sizeof(local)) < 0) {
        puts("dhcp: bind failed");
        closesocket(sd);
        return;
    }

    struct dhcp_msg msg;
    memset(&msg, 0, sizeof(msg));
    uint8_t mac[6];
    uint32_t cur_ip;
    if (net_info(mac, &cur_ip) == 0) {
        for (int i = 0; i < 6; i++)
            msg.chaddr[i] = mac[i];
    }
    msg.op = 1;
    msg.htype = 1;
    msg.hlen = 6;
    msg.xid = 0x78563412u;
    msg.flags = htons(0x8000);
    msg.magic[0] = 99; msg.magic[1] = 130; msg.magic[2] = 83; msg.magic[3] = 99;
    int pos = 0;
    uint8_t type = 1;
    pos = dhcp_add_opt(msg.opts, pos, 53, &type, 1);
    uint8_t params[] = { 1, 3, 6 };
    pos = dhcp_add_opt(msg.opts, pos, 55, params, sizeof(params));
    msg.opts[pos++] = 255;

    struct sockaddr_in dst;
    dst.sin_family = AF_INET;
    dst.sin_port = htons(67);
    dst.sin_addr = INADDR_BROADCAST;
    if (sendto(sd, &msg, sizeof(msg), 0, &dst, sizeof(dst)) < 0) {
        puts("dhcp: discover send failed");
        closesocket(sd);
        return;
    }

    struct sockaddr_in from;
    int n = recvfrom(sd, &msg, sizeof(msg), 0, &from, sizeof(from));
    if (n < 0) {
        puts("dhcp: no offer");
        closesocket(sd);
        return;
    }
    printf("dhcp offer ");
    print_ipv4(msg.yiaddr);
    putchar('\n');
    closesocket(sd);
}

static void cmd_pipetest(void) {
    int fds[2];
    if (pipe(fds) < 0) {
        puts("pipe: failed");
        return;
    }
    const char *msg = "hello through pipe";
    if (write(fds[1], msg, strlen(msg)) < 0) {
        puts("pipe: write failed");
        close(fds[0]);
        close(fds[1]);
        return;
    }
    char buf[64];
    int n = read(fds[0], buf, sizeof(buf) - 1);
    if (n < 0) {
        puts("pipe: read failed");
    } else {
        buf[n] = 0;
        printf("pipe: %s\n", buf);
    }
    close(fds[0]);
    close(fds[1]);
}

static void futex_demo_thread(void) {
    sleep_ms(200);
    futex_demo_word = 1;
    futex_wake(&futex_demo_word, 1);
}

static void cmd_futextest(void) {
    futex_demo_word = 0;
    int tid = spawn(futex_demo_thread);
    if (tid < 0) {
        puts("futex: spawn failed");
        return;
    }
    while (futex_demo_word == 0)
        futex_wait(&futex_demo_word, 0);
    join(tid);
    puts("futex: woke");
}

static void execute(char *line) {
    trim_right(line);
    if (!line[0])
        return;
    if (starts_with(line, "help")) cmd_help();
    else if (starts_with(line, "ls ")) cmd_ls(line + 3);
    else if (starts_with(line, "ls")) cmd_ls("");
    else if (starts_with(line, "cd ")) {
        if (chdir(line + 3) < 0) puts("cd: not a directory");
    } else if (starts_with(line, "cd")) {
        if (chdir("/") < 0) puts("cd: failed");
    } else if (starts_with(line, "pwd")) cmd_pwd();
    else if (starts_with(line, "stat ")) cmd_stat(line + 5);
    else if (starts_with(line, "cat ")) cmd_cat(line + 4);
    else if (starts_with(line, "ping ")) cmd_ping(line + 5);
    else if (starts_with(line, "wget ")) cmd_sockget(line + 5);
    else if (starts_with(line, "dhcp")) cmd_dhcp();
    else if (starts_with(line, "pipetest")) cmd_pipetest();
    else if (starts_with(line, "futextest")) cmd_futextest();
    else if (starts_with(line, "mkdir ")) {
        if (mkdir(line + 6) < 0) puts("mkdir: failed");
    } else if (starts_with(line, "rmdir ")) {
        if (rmdir(line + 6) < 0) puts("rmdir: failed");
    } else if (starts_with(line, "touch ")) {
        if (create(line + 6) < 0) puts("touch: failed");
    } else if (starts_with(line, "write ")) cmd_write_file(line + 6);
    else if (starts_with(line, "rm ")) {
        if (unlink(line + 3) < 0) puts("rm: failed");
    } else if (starts_with(line, "mv ")) {
        char local[128];
        int n = 0;
        while (line[3 + n] && n < (int)sizeof(local) - 1) {
            local[n] = line[3 + n];
            n++;
        }
        local[n] = 0;
        char *argv[2];
        if (split_args(local, argv, 2) != 2 || rename(argv[0], argv[1]) < 0)
            puts("mv: failed");
    }
    else if (starts_with(line, "nano ")) cmd_nano(line + 5);
    else if (starts_with(line, "nano")) cmd_nano("");
    else if (starts_with(line, "basm ")) cmd_basm(line + 5);
    else if (starts_with(line, "basm")) cmd_basm("");
    else if (starts_with(line, "exec ")) cmd_exec(line + 5);
    else if (starts_with(line, "wait ")) cmd_wait(line + 5);
    else if (starts_with(line, "wait")) cmd_wait("");
    else if (starts_with(line, "kill ")) {
        if (kill(parse_u32(line + 5)) < 0) puts("kill: failed");
    } else if (starts_with(line, "ps ")) cmd_ps(line + 3);
    else if (starts_with(line, "ps")) cmd_ps("");
    else if (starts_with(line, "echo ")) puts(line + 5);
    else if (starts_with(line, "sleep ")) sleep_ms((unsigned int)parse_u32(line + 6) * 1000u);
    else if (starts_with(line, "reboot")) reboot();
    else if (line[0] == '/') cmd_exec(line);
    else puts("? try help");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    puts("=== BuzzOS User Shell ===");
    char line[LINE_MAX];
    for (;;) {
        prompt();
        read_line(line, sizeof(line));
        trim_right(line);
        history_add(line);
        execute(line);
    }
}
