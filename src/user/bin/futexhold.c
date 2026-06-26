#include "libc.h"

static int futex_word;

static void waiter(void) {
    futex_wait(&futex_word, 0);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    futex_word = 0;
    int tid = spawn(waiter);
    if (tid < 0) {
        puts("futexhold: spawn failed");
        return 2;
    }
    puts("futexhold: waiting");
    for (;;)
        sleep_ms(1000);
    return 0;
}
