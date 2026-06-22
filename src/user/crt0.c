#include "libc.h"

/* User program provides main(). */
extern int main(int argc, char **argv);

__attribute__((section(".text.entry")))
void _start(void) {
    int argc;
    char **argv;
    __asm__ volatile("mov 4(%%esp), %0" : "=r"(argc));
    __asm__ volatile("mov 8(%%esp), %0" : "=r"(argv));
    int ret = main(argc, argv);
    exit(ret);
}
