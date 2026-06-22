#ifndef BUZZOS_TASK_H
#define BUZZOS_TASK_H

#include <stdint.h>

/* Process Control Block. The saved register context is stored on the
 * task's own kernel stack; the PCB only holds the stack pointer. */
struct task {
    uint32_t esp;        /* saved stack pointer */
    uint32_t kstack;     /* top of kernel stack (for cleanup) */
    int      id;         /* task id */
    int      state;      /* 0 = ready, 1 = running, 2 = blocked */
    char     name[16];
};

#define TASK_READY   0
#define TASK_RUNNING 1
#define MAX_TASKS    8

/* Initialise the scheduler. Creates an idle task from the current
 * execution context. */
void sched_init(void);

/* Create a new kernel thread starting at `entry`. Returns task id. */
int task_create(void (*entry)(void), const char *name);

/* Yield the CPU voluntarily. */
void task_yield(void);

/* Called from timer IRQ to preempt the current task. */
void sched_tick(void);

/* The currently running task. */
extern struct task *current_task;

#endif /* BUZZOS_TASK_H */
