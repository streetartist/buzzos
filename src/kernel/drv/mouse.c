#include "io.h"
#include "mouse.h"
#include "vga.h"

enum {
    PS2_DATA = 0x60,
    PS2_STATUS = 0x64,
    PS2_CMD = 0x64,
    PIC1_DATA = 0x21,
    PIC2_DATA = 0xA1,
};

static volatile int mouse_x;
static volatile int mouse_y;
static volatile int mouse_buttons;
static volatile int mouse_dx;
static volatile int mouse_dy;
static volatile uint32_t mouse_seq;
static volatile int mouse_enabled;

static uint8_t packet[3];
static int packet_index;

static int ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS) & 0x02) == 0)
            return 0;
    }
    return -1;
}

static int ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01)
            return 0;
    }
    return -1;
}

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & (1u << 9))
        __asm__ volatile("sti" ::: "memory");
}

static void ps2_flush_output(void) {
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & 0x01); i++)
        (void)inb(PS2_DATA);
}

static int ps2_read_cmd_byte(uint8_t *cmd) {
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_CMD, 0x20);
    if (ps2_wait_read() < 0)
        return -1;
    *cmd = inb(PS2_DATA);
    return 0;
}

static int ps2_write_cmd_byte(uint8_t cmd) {
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_CMD, 0x60);
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_DATA, cmd);
    return 0;
}

static int mouse_send(uint8_t cmd) {
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_CMD, 0xD4);
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_DATA, cmd);
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(PS2_STATUS);
        if ((status & 0x01) == 0)
            continue;
        uint8_t data = inb(PS2_DATA);
        if ((status & 0x20) == 0)
            continue;
        return data == 0xFA ? 0 : -1;
    }
    return -1;
}

static void disable_mouse_port(void) {
    if (ps2_wait_write() == 0)
        outb(PS2_CMD, 0xA7);
}

static void enable_keyboard_port(void) {
    if (ps2_wait_write() == 0)
        outb(PS2_CMD, 0xAE);
}

static void unmask_irq12(void) {
    uint8_t master = inb(PIC1_DATA);
    uint8_t slave = inb(PIC2_DATA);
    master &= (uint8_t)~0x04;  /* IRQ2 cascade */
    slave &= (uint8_t)~0x10;   /* IRQ12 PS/2 mouse */
    outb(PIC1_DATA, master);
    outb(PIC2_DATA, slave);
}

void mouse_init(void) {
    mouse_x = VGA_GFX_WIDTH / 2;
    mouse_y = VGA_GFX_HEIGHT / 2;
    mouse_buttons = 0;
    mouse_dx = 0;
    mouse_dy = 0;
    mouse_seq = 0;
    mouse_enabled = 0;
    packet_index = 0;

    uint32_t flags = irq_save();
    uint8_t old_cmd = 0;
    int have_cmd = 0;
    ps2_flush_output();
    if (ps2_wait_write() == 0)
        outb(PS2_CMD, 0xAD);   /* keep keyboard bytes out of 0x60 while programming */
    disable_mouse_port();
    ps2_flush_output();

    if (ps2_read_cmd_byte(&old_cmd) == 0)
        have_cmd = 1;

    if (ps2_wait_write() == 0)
        outb(PS2_CMD, 0xA8);   /* enable auxiliary PS/2 device */

    int ok = 1;
    if (have_cmd) {
        uint8_t cmd = old_cmd;
        cmd |= 0x03;           /* IRQ1 keyboard and IRQ12 mouse */
        cmd &= (uint8_t)~0x30;  /* enable keyboard and mouse clocks */
        if (ps2_write_cmd_byte(cmd) < 0)
            ok = 0;
    } else {
        ok = 0;
    }

    ps2_flush_output();
    (void)mouse_send(0xF5);     /* stop streaming while resetting */
    if (mouse_send(0xF6) < 0)   /* defaults */
        ok = 0;
    if (mouse_send(0xF4) < 0)   /* enable data reporting */
        ok = 0;

    if (ok) {
        mouse_enabled = 1;
        unmask_irq12();
    } else {
        disable_mouse_port();
        if (have_cmd) {
            uint8_t cmd = old_cmd;
            cmd |= 0x01;        /* keep keyboard IRQ enabled */
            cmd &= (uint8_t)~0x10;
            cmd &= (uint8_t)~0x02;
            cmd |= 0x20;        /* disable mouse clock */
            (void)ps2_write_cmd_byte(cmd);
        }
    }
    enable_keyboard_port();
    irq_restore(flags);
}

void mouse_handler(uint8_t byte) {
    if (!mouse_enabled)
        return;
    if (packet_index == 0 && (byte & 0x08) == 0)
        return;

    packet[packet_index++] = byte;
    if (packet_index < 3)
        return;
    packet_index = 0;

    int buttons = packet[0] & 0x07;
    int dx = (int)(int8_t)packet[1];
    int dy = (int)(int8_t)packet[2];

    if (packet[0] & 0xC0) {
        dx = 0;
        dy = 0;
    }

    int x = mouse_x + dx;
    int y = mouse_y - dy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= VGA_GFX_WIDTH) x = VGA_GFX_WIDTH - 1;
    if (y >= VGA_GFX_HEIGHT) y = VGA_GFX_HEIGHT - 1;

    mouse_x = x;
    mouse_y = y;
    mouse_buttons = buttons;
    mouse_dx = dx;
    mouse_dy = -dy;
    mouse_seq++;
}

void mouse_get_state(struct mouse_state *out) {
    if (!out)
        return;
    out->x = mouse_x;
    out->y = mouse_y;
    out->buttons = mouse_buttons;
    out->dx = mouse_dx;
    out->dy = mouse_dy;
    out->seq = mouse_seq;
}
