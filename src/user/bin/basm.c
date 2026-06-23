#include "libc.h"

#define SRC_MAX 4096
#define TEXT_MAX 2048
#define DATA_MAX 1536
#define ELF_MAX 4096
#define MAX_SYMS 128
#define NAME_MAX 31

#define USER_BASE 0x200000u
#define LOAD_OFF  0x100u

enum section_id {
    SEC_TEXT = 0,
    SEC_DATA = 1,
};

struct sym {
    int used;
    int is_equ;
    int section;
    uint32_t value;
    char name[NAME_MAX + 1];
};

static char source[SRC_MAX + 1];
static uint8_t text_buf[TEXT_MAX];
static uint8_t data_buf[DATA_MAX];
static uint8_t elf_buf[ELF_MAX];
static struct sym syms[MAX_SYMS];

static int src_len;
static int text_len;
static int data_len;
static int final_text_len;
static int cur_section;
static int pass_no;
static int line_no;
static int failed;
static char err_msg[96];

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_name_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.';
}

static int is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static char lower_char(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static void set_error(const char *msg) {
    if (failed)
        return;
    failed = 1;
    int i = 0;
    while (msg[i] && i < (int)sizeof(err_msg) - 1) {
        err_msg[i] = msg[i];
        i++;
    }
    err_msg[i] = 0;
}

static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static void rtrim(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && is_space(s[n - 1]))
        s[--n] = 0;
}

static void strip_comment(char *s) {
    int quote = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\'))
            quote = !quote;
        if (!quote && s[i] == ';') {
            s[i] = 0;
            return;
        }
    }
}

static int parse_token(char **pp, char *out, int out_size) {
    char *p = skip_ws(*pp);
    int n = 0;
    if (!is_name_start(*p) && *p != '%')
        return 0;
    while ((is_name_char(*p) || *p == '%') && n < out_size - 1)
        out[n++] = lower_char(*p++);
    out[n] = 0;
    *pp = p;
    return n > 0;
}

static int parse_number(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int i = 0;
    int base = 10;
    if (s[0] == '0' && lower_char(s[1]) == 'x') {
        base = 16;
        i = 2;
    }
    if (!s[i])
        return 0;
    for (; s[i]; i++) {
        char c = lower_char(s[i]);
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return 0;
        if (d >= base)
            return 0;
        v = v * (uint32_t)base + (uint32_t)d;
    }
    *out = v;
    return 1;
}

static struct sym *find_sym(const char *name) {
    for (int i = 0; i < MAX_SYMS; i++) {
        if (syms[i].used && streq(syms[i].name, name))
            return &syms[i];
    }
    return 0;
}

static struct sym *get_sym_slot(const char *name) {
    struct sym *s = find_sym(name);
    if (s)
        return s;
    for (int i = 0; i < MAX_SYMS; i++) {
        if (!syms[i].used) {
            syms[i].used = 1;
            syms[i].is_equ = 0;
            syms[i].section = SEC_TEXT;
            syms[i].value = 0;
            int n = 0;
            while (name[n] && n < NAME_MAX) {
                syms[i].name[n] = name[n];
                n++;
            }
            syms[i].name[n] = 0;
            return &syms[i];
        }
    }
    set_error("too many symbols");
    return 0;
}

static void define_label(const char *name) {
    struct sym *s = get_sym_slot(name);
    if (!s)
        return;
    s->is_equ = 0;
    s->section = cur_section;
    s->value = (uint32_t)(cur_section == SEC_TEXT ? text_len : data_len);
}

static void define_equ(const char *name, uint32_t value) {
    struct sym *s = get_sym_slot(name);
    if (!s)
        return;
    s->is_equ = 1;
    s->section = SEC_TEXT;
    s->value = value;
}

static uint32_t section_offset(void) {
    return (uint32_t)(cur_section == SEC_TEXT ? text_len : data_len);
}

static uint32_t sym_runtime_value(const struct sym *s, int dollar_expr) {
    if (s->is_equ)
        return s->value;
    if (dollar_expr)
        return s->value;
    if (s->section == SEC_TEXT)
        return USER_BASE + s->value;
    return USER_BASE + (uint32_t)final_text_len + s->value;
}

static int expr_has_dollar(const char *expr) {
    for (int i = 0; expr[i]; i++)
        if (expr[i] == '$')
            return 1;
    return 0;
}

static int eval_term(const char *term, int dollar_expr, uint32_t *out) {
    char local[64];
    int n = 0;
    while (term[n] && n < (int)sizeof(local) - 1) {
        local[n] = lower_char(term[n]);
        n++;
    }
    local[n] = 0;
    while (n > 0 && is_space(local[n - 1]))
        local[--n] = 0;
    char *p = local;
    p = skip_ws(p);
    if (!*p)
        return 0;
    if (streq(p, "$")) {
        *out = section_offset();
        return 1;
    }
    if (parse_number(p, out))
        return 1;
    struct sym *s = find_sym(p);
    if (!s) {
        if (pass_no == 1) {
            *out = 0;
            return 1;
        }
        set_error("undefined symbol");
        return 0;
    }
    *out = sym_runtime_value(s, dollar_expr);
    return 1;
}

static int eval_expr(const char *expr, uint32_t *out) {
    int dollar_expr = expr_has_dollar(expr);
    uint32_t acc = 0;
    int sign = 1;
    int have = 0;
    int start = 0;
    int len = (int)strlen(expr);

    for (int i = 0; i <= len; i++) {
        char c = expr[i];
        if (c == '+' || c == '-' || c == 0) {
            char term[64];
            int n = 0;
            for (int j = start; j < i && n < (int)sizeof(term) - 1; j++)
                term[n++] = expr[j];
            term[n] = 0;
            uint32_t v = 0;
            if (!eval_term(term, dollar_expr, &v))
                return 0;
            if (sign > 0)
                acc += v;
            else
                acc -= v;
            have = 1;
            sign = (c == '-') ? -1 : 1;
            start = i + 1;
        }
    }
    if (!have)
        return 0;
    *out = acc;
    return 1;
}

static int reg_id(const char *s) {
    if (streq(s, "eax")) return 0;
    if (streq(s, "ecx")) return 1;
    if (streq(s, "edx")) return 2;
    if (streq(s, "ebx")) return 3;
    if (streq(s, "esp")) return 4;
    if (streq(s, "ebp")) return 5;
    if (streq(s, "esi")) return 6;
    if (streq(s, "edi")) return 7;
    return -1;
}

static void emit_byte(uint8_t b) {
    if (cur_section == SEC_TEXT) {
        if (text_len >= TEXT_MAX) {
            set_error("text too large");
            return;
        }
        if (pass_no == 2)
            text_buf[text_len] = b;
        text_len++;
    } else {
        if (data_len >= DATA_MAX) {
            set_error("data too large");
            return;
        }
        if (pass_no == 2)
            data_buf[data_len] = b;
        data_len++;
    }
}

static void emit_u32(uint32_t v) {
    emit_byte((uint8_t)(v & 0xFF));
    emit_byte((uint8_t)((v >> 8) & 0xFF));
    emit_byte((uint8_t)((v >> 16) & 0xFF));
    emit_byte((uint8_t)((v >> 24) & 0xFF));
}

static uint32_t current_runtime_addr(void) {
    if (cur_section == SEC_TEXT)
        return USER_BASE + (uint32_t)text_len;
    return USER_BASE + (uint32_t)final_text_len + (uint32_t)data_len;
}

static int split_operands(char *s, char ops[][64], int max_ops) {
    int count = 0;
    int quote = 0;
    int pos = 0;
    for (int i = 0;; i++) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i - 1] != '\\'))
            quote = !quote;
        if ((c == ',' && !quote) || c == 0) {
            if (count >= max_ops)
                return count;
            ops[count][pos] = 0;
            rtrim(ops[count]);
            char *p = skip_ws(ops[count]);
            if (p != ops[count]) {
                int j = 0;
                while (p[j]) {
                    ops[count][j] = p[j];
                    j++;
                }
                ops[count][j] = 0;
            }
            count++;
            pos = 0;
            if (c == 0)
                break;
            continue;
        }
        if (pos < 63)
            ops[count][pos++] = lower_char(c);
    }
    if (count == 1 && ops[0][0] == 0)
        return 0;
    return count;
}

static void emit_db(char *args) {
    char *p = args;
    while (*p) {
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (!*p)
            break;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                char c = *p++;
                if (c == '\\' && *p) {
                    char e = *p++;
                    if (e == 'n') c = '\n';
                    else if (e == 'r') c = '\r';
                    else if (e == 't') c = '\t';
                    else c = e;
                }
                emit_byte((uint8_t)c);
            }
            if (*p == '"')
                p++;
        } else {
            char term[64];
            int n = 0;
            while (*p && *p != ',' && n < (int)sizeof(term) - 1)
                term[n++] = *p++;
            term[n] = 0;
            uint32_t v = 0;
            if (!eval_expr(term, &v))
                return;
            emit_byte((uint8_t)v);
        }
        p = skip_ws(p);
        if (*p == ',')
            p++;
    }
}

static void emit_dd(char *args) {
    char ops[16][64];
    int n = split_operands(args, ops, 16);
    for (int i = 0; i < n; i++) {
        uint32_t v = 0;
        if (!eval_expr(ops[i], &v))
            return;
        emit_u32(v);
    }
}

static void emit_jcc(const char *mn, char *op) {
    uint8_t cc;
    if (streq(mn, "je") || streq(mn, "jz")) cc = 0x84;
    else if (streq(mn, "jne") || streq(mn, "jnz")) cc = 0x85;
    else if (streq(mn, "jl")) cc = 0x8C;
    else if (streq(mn, "jge")) cc = 0x8D;
    else if (streq(mn, "jle")) cc = 0x8E;
    else if (streq(mn, "jg")) cc = 0x8F;
    else {
        set_error("unsupported conditional jump");
        return;
    }
    uint32_t cur = current_runtime_addr();
    uint32_t target = 0;
    if (!eval_expr(op, &target))
        return;
    emit_byte(0x0F);
    emit_byte(cc);
    emit_u32(target - (cur + 6));
}

static void emit_instruction(const char *mn, char *args) {
    char ops[3][64];
    int n = split_operands(args, ops, 3);

    if (streq(mn, "db")) {
        emit_db(args);
        return;
    }
    if (streq(mn, "dd")) {
        emit_dd(args);
        return;
    }
    if (streq(mn, "nop")) {
        emit_byte(0x90);
        return;
    }
    if (streq(mn, "ret")) {
        emit_byte(0xC3);
        return;
    }
    if (streq(mn, "int")) {
        if (n != 1) { set_error("int needs one operand"); return; }
        uint32_t v = 0;
        if (!eval_expr(ops[0], &v)) return;
        emit_byte(0xCD);
        emit_byte((uint8_t)v);
        return;
    }
    if (streq(mn, "push") || streq(mn, "pop")) {
        if (n != 1) { set_error("push/pop needs one operand"); return; }
        int r = reg_id(ops[0]);
        if (r < 0) { set_error("bad register"); return; }
        emit_byte((uint8_t)((streq(mn, "push") ? 0x50 : 0x58) + r));
        return;
    }
    if (streq(mn, "mov")) {
        if (n != 2) { set_error("mov needs two operands"); return; }
        int dst = reg_id(ops[0]);
        int src = reg_id(ops[1]);
        if (dst < 0) { set_error("bad mov destination"); return; }
        if (src >= 0) {
            emit_byte(0x89);
            emit_byte((uint8_t)(0xC0 | (src << 3) | dst));
            return;
        }
        uint32_t v = 0;
        if (!eval_expr(ops[1], &v)) return;
        emit_byte((uint8_t)(0xB8 + dst));
        emit_u32(v);
        return;
    }
    if (streq(mn, "xor")) {
        if (n != 2) { set_error("xor needs two operands"); return; }
        int dst = reg_id(ops[0]);
        int src = reg_id(ops[1]);
        if (dst < 0 || src < 0) { set_error("xor supports registers only"); return; }
        emit_byte(0x31);
        emit_byte((uint8_t)(0xC0 | (src << 3) | dst));
        return;
    }
    if (streq(mn, "add") || streq(mn, "sub") || streq(mn, "cmp")) {
        if (n != 2) { set_error("arithmetic needs two operands"); return; }
        int dst = reg_id(ops[0]);
        if (dst < 0) { set_error("bad arithmetic destination"); return; }
        uint32_t v = 0;
        if (!eval_expr(ops[1], &v)) return;
        uint8_t modrm = (uint8_t)(0xC0 | dst);
        if (streq(mn, "sub")) modrm = (uint8_t)(0xE8 | dst);
        if (streq(mn, "cmp")) modrm = (uint8_t)(0xF8 | dst);
        emit_byte(0x81);
        emit_byte(modrm);
        emit_u32(v);
        return;
    }
    if (streq(mn, "call") || streq(mn, "jmp")) {
        if (n != 1) { set_error("jump needs one operand"); return; }
        uint32_t cur = current_runtime_addr();
        uint32_t target = 0;
        if (!eval_expr(ops[0], &target)) return;
        emit_byte(streq(mn, "call") ? 0xE8 : 0xE9);
        emit_u32(target - (cur + 5));
        return;
    }
    if (mn[0] == 'j') {
        if (n != 1) { set_error("conditional jump needs one operand"); return; }
        emit_jcc(mn, ops[0]);
        return;
    }

    set_error("unsupported instruction");
}

static int handle_equ_line(char *line) {
    char *p = line;
    char name[NAME_MAX + 1];
    if (!parse_token(&p, name, sizeof(name)))
        return 0;
    char *after_name = p;
    char tok[16];
    if (!parse_token(&p, tok, sizeof(tok)))
        return 0;
    if (!streq(tok, "equ"))
        return 0;
    uint32_t v = 0;
    if (!eval_expr(p, &v))
        return 1;
    define_equ(name, v);
    (void)after_name;
    return 1;
}

static void process_line(char *line) {
    strip_comment(line);
    rtrim(line);
    char *p = skip_ws(line);
    if (!*p)
        return;

    if (p[0] == '%' && p[1]) {
        char tok[32];
        parse_token(&p, tok, sizeof(tok));
        if (streq(tok, "%define")) {
            char name[NAME_MAX + 1];
            if (!parse_token(&p, name, sizeof(name))) {
                set_error("bad define");
                return;
            }
            uint32_t v = 0;
            if (!eval_expr(p, &v))
                return;
            define_equ(name, v);
            return;
        }
        set_error("unsupported directive");
        return;
    }

    if (handle_equ_line(p))
        return;

    char first[NAME_MAX + 1];
    char *label_scan = p;
    if (parse_token(&label_scan, first, sizeof(first))) {
        label_scan = skip_ws(label_scan);
        if (*label_scan == ':') {
            define_label(first);
            p = skip_ws(label_scan + 1);
            if (!*p)
                return;
        }
    }

    char mnemonic[32];
    if (!parse_token(&p, mnemonic, sizeof(mnemonic)))
        return;
    p = skip_ws(p);

    if (streq(mnemonic, "bits") || streq(mnemonic, "global") || streq(mnemonic, "extern"))
        return;
    if (streq(mnemonic, "section")) {
        if (p[0] == '.')
            p++;
        if (streq(p, "text"))
            cur_section = SEC_TEXT;
        else if (streq(p, "rodata") || streq(p, "data"))
            cur_section = SEC_DATA;
        else
            set_error("unknown section");
        return;
    }

    emit_instruction(mnemonic, p);
}

static int assemble_pass(void) {
    text_len = 0;
    data_len = 0;
    cur_section = SEC_TEXT;
    line_no = 1;
    int pos = 0;

    while (pos < src_len && !failed) {
        char line[192];
        int n = 0;
        while (pos < src_len && source[pos] != '\n' && n < (int)sizeof(line) - 1)
            line[n++] = source[pos++];
        while (pos < src_len && source[pos] != '\n')
            pos++;
        if (pos < src_len && source[pos] == '\n')
            pos++;
        line[n] = 0;
        process_line(line);
        line_no++;
    }
    return failed ? -1 : 0;
}

static void put16(int off, uint16_t v) {
    elf_buf[off] = (uint8_t)(v & 0xFF);
    elf_buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put32(int off, uint32_t v) {
    elf_buf[off] = (uint8_t)(v & 0xFF);
    elf_buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    elf_buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    elf_buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static int build_elf(void) {
    int seg_size = text_len + data_len;
    int file_size = (int)LOAD_OFF + seg_size;
    if (file_size > ELF_MAX) {
        set_error("ELF too large for /fs");
        return -1;
    }

    memset(elf_buf, 0, sizeof(elf_buf));
    elf_buf[0] = 0x7F;
    elf_buf[1] = 'E';
    elf_buf[2] = 'L';
    elf_buf[3] = 'F';
    elf_buf[4] = 1;
    elf_buf[5] = 1;
    elf_buf[6] = 1;
    put16(0x10, 2);
    put16(0x12, 3);
    put32(0x14, 1);

    struct sym *entry = find_sym("_start");
    uint32_t entry_addr = USER_BASE;
    if (entry)
        entry_addr = sym_runtime_value(entry, 0);
    put32(0x18, entry_addr);
    put32(0x1C, 52);
    put32(0x20, 0);
    put32(0x24, 0);
    put16(0x28, 52);
    put16(0x2A, 32);
    put16(0x2C, 1);

    int ph = 52;
    put32(ph + 0, 1);
    put32(ph + 4, LOAD_OFF);
    put32(ph + 8, USER_BASE);
    put32(ph + 12, USER_BASE);
    put32(ph + 16, (uint32_t)seg_size);
    put32(ph + 20, (uint32_t)seg_size);
    put32(ph + 24, 7);
    put32(ph + 28, 0x1000);

    memcpy(elf_buf + LOAD_OFF, text_buf, (size_t)text_len);
    memcpy(elf_buf + LOAD_OFF + text_len, data_buf, (size_t)data_len);
    return file_size;
}

static int read_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    src_len = 0;
    for (;;) {
        int room = SRC_MAX - src_len;
        if (room <= 0) {
            close(fd);
            return -1;
        }
        int n = read(fd, source + src_len, (size_t)room);
        if (n <= 0)
            break;
        src_len += n;
    }
    close(fd);
    source[src_len] = 0;
    return 0;
}

static int write_file(const char *path, const uint8_t *buf, int len) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0)
        return -1;
    int off = 0;
    while (off < len) {
        int n = write(fd, buf + off, (size_t)(len - off));
        if (n <= 0) {
            close(fd);
            return -1;
        }
        off += n;
    }
    close(fd);
    return 0;
}

static void default_output_path(const char *src, char *out, int out_size) {
    int n = 0;
    while (src[n] && n < out_size - 1) {
        out[n] = src[n];
        n++;
    }
    out[n] = 0;
    if (n > 4 && out[n - 4] == '.' && out[n - 3] == 'a' &&
        out[n - 2] == 's' && out[n - 1] == 'm') {
        out[n - 4] = 0;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: basm <input.asm> [output]");
        puts("example: basm /fs/demo.asm /fs/demo");
        return 1;
    }

    const char *in_path = argv[1];
    char out_path[128];
    if (argc >= 3) {
        int i = 0;
        while (argv[2][i] && i < (int)sizeof(out_path) - 1) {
            out_path[i] = argv[2][i];
            i++;
        }
        out_path[i] = 0;
    } else {
        default_output_path(in_path, out_path, sizeof(out_path));
    }

    if (read_file(in_path) < 0) {
        puts("basm: read failed");
        return 1;
    }

    memset(syms, 0, sizeof(syms));
    failed = 0;
    pass_no = 1;
    if (assemble_pass() < 0)
        goto fail;
    final_text_len = text_len;

    pass_no = 2;
    if (assemble_pass() < 0)
        goto fail;

    int elf_size = build_elf();
    if (elf_size < 0)
        goto fail;
    if (write_file(out_path, elf_buf, elf_size) < 0) {
        puts("basm: write failed");
        return 1;
    }

    printf("basm: wrote %s (%d bytes)\n", out_path, elf_size);
    return 0;

fail:
    printf("basm: line %d: %s\n", line_no, err_msg[0] ? err_msg : "error");
    return 1;
}
