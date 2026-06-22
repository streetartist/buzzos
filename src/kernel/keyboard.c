#include "keyboard.h"
#include "io.h"

#define BUF_SIZE 256

static volatile uint8_t buf[BUF_SIZE];
static volatile int     head, tail;

/* US QWERTY scancode → ASCII (scancode set 1, unshifted) */
static const char scancode_ascii[128] = {
    0,   0x1B, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0,   ' ',0,  0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
};

void keyboard_init(void) {
    head = tail = 0;
}

void keyboard_handler(uint8_t scancode) {
    if (scancode & 0x80) return;   /* key release — ignore */
    if (scancode >= 128) return;
    char c = scancode_ascii[scancode];
    if (c == 0) return;

    int next = (tail + 1) % BUF_SIZE;
    if (next == head) return;      /* buffer full */
    buf[tail] = (uint8_t)c;
    tail = next;
}

int keyboard_getchar(void) {
    if (head == tail) return -1;   /* empty */
    int c = buf[head];
    head = (head + 1) % BUF_SIZE;
    return c;
}
