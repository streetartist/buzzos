#ifndef BUZZOS_SYSCALL_H
#define BUZZOS_SYSCALL_H
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_VECTOR        0x80
#define SYSCALL_VECTOR_LEGACY 0x30

struct syscall_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

enum { SYS_EXIT=1, SYS_OPEN=2, SYS_CLOSE=3, SYS_READ=4, SYS_WRITE=5 };

void syscall_init(void);
void syscall_handler(struct syscall_frame *frame);
#endif
