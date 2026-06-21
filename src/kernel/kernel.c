#include <stddef.h>
#include <stdint.h>

enum {
    VGA_WIDTH = 80,
    VGA_HEIGHT = 25,
    VGA_ADDR = 0xb8000,
};

static volatile uint16_t *const vga = (uint16_t *)VGA_ADDR;
static uint8_t row;
static uint8_t col;
static uint8_t color = 0x0f;

extern uint8_t __bss_start;
extern uint8_t __bss_end;

static void zero_bss(void)
{
    for (uint8_t *p = &__bss_start; p < &__bss_end; p++) {
        *p = 0;
    }
}

static uint16_t vga_cell(char ch)
{
    return (uint16_t)(uint8_t)ch | ((uint16_t)color << 8);
}

static void clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = vga_cell(' ');
    }
    row = 0;
    col = 0;
}

static void newline(void)
{
    col = 0;
    if (++row < VGA_HEIGHT) {
        return;
    }

    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }

    row = VGA_HEIGHT - 1;
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga[row * VGA_WIDTH + x] = vga_cell(' ');
    }
}

static void putc(char ch)
{
    if (ch == '\n') {
        newline();
        return;
    }

    vga[row * VGA_WIDTH + col] = vga_cell(ch);
    if (++col == VGA_WIDTH) {
        newline();
    }
}

static void puts(const char *s)
{
    while (*s) {
        putc(*s++);
    }
}

__attribute__((section(".text.entry"), used, noreturn))
void _start(void)
{
    zero_bss();
    clear();
    puts("BuzzOS\n");
    puts("minimal i686 kernel\n\n");
    puts("next: memory, syscalls, ELF, VFS, framebuffer\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
