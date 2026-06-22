#include "libc.h"

#define LINE_MAX 128

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

static int read_line(char *line, int size) {
    int len = 0;
    for (;;) {
        char c;
        int n = read(0, &c, 1);
        if (n <= 0) {
            yield();
            continue;
        }
        if (c == '\r')
            c = '\n';
        if (c == '\n') {
            putchar('\n');
            line[len] = 0;
            return len;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && len < size - 1) {
            line[len++] = c;
            putchar(c);
        }
    }
}

static void cmd_help(void) {
    puts("ls cd pwd stat cat mkdir rmdir touch write rm mv ping wget dhcp exec wait kill ps echo sleep reboot help");
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
        execute(line);
    }
}
