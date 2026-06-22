#ifndef BUZZOS_SYSCALL_H
#define BUZZOS_SYSCALL_H
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_VECTOR        0x80
#define SYSCALL_VECTOR_LEGACY 0x30

struct syscall_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

enum { SYS_EXIT=1, SYS_OPEN=2, SYS_CLOSE=3, SYS_READ=4, SYS_WRITE=5,
       SYS_SPAWN=6, SYS_YIELD=7, SYS_JOIN=8, SYS_SLEEP=9, SYS_KILL=10,
       SYS_GETPID=11, SYS_GETTID=12, SYS_CHDIR=13, SYS_GETCWD=14,
       SYS_WAITPID=15, SYS_DUP=16, SYS_DUP2=17,
       SYS_STAT=18, SYS_GETDENTS=19, SYS_SPAWN_PROC=20,
       SYS_PS=21, SYS_REBOOT=22, SYS_MKDIR=23, SYS_UNLINK=24,
       SYS_CREATE=25, SYS_SPAWN_PROC_ARGS=26, SYS_LSEEK=27,
       SYS_RMDIR=28, SYS_RENAME=29, SYS_SOCKET=30,
       SYS_CONNECT=31, SYS_SEND=32, SYS_RECV=33,
       SYS_CLOSESOCKET=34, SYS_DNS_RESOLVE=35, SYS_BIND=36,
       SYS_SENDTO=37, SYS_RECVFROM=38, SYS_NETINFO=39 };

void syscall_init(void);
void syscall_handler(struct syscall_frame *frame);
void syscall_reset_process(int task_id);
#endif
