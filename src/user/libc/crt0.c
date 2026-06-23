#include "libc.h"

/* User program provides main(). */
extern int main(int argc, char **argv);

__asm__(
    ".section .text.entry,\"ax\",@progbits\n"
    ".globl _start\n"
    "_start:\n"
    "    movl 4(%esp), %eax\n"
    "    movl 8(%esp), %ecx\n"
    "    pushl %ecx\n"
    "    pushl %eax\n"
    "    calll main\n"
    "    addl $8, %esp\n"
    "    pushl %eax\n"
    "    calll exit\n"
    "1:\n"
    "    jmp 1b\n"
);
