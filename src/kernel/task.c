#include "task.h"
#include "pmm.h"
#include "serial.h"

/* ------------------------------------------------------------------ */
/*  Assembly helper                                                     */
/* ------------------------------------------------------------------ */
extern void switch_context(uint32_t *old_esp_ptr, uint32_t *new_esp);

/* ------------------------------------------------------------------ */
/*  Globals                                                             */
/* ------------------------------------------------------------------ */

static struct task tasks[MAX_TASKS];
static int          num_tasks;
static int          current_id;
struct task        *current_task;

/* ------------------------------------------------------------------ */
/*  Idle task — runs when nothing else is ready                         */
/* ------------------------------------------------------------------ */

static void idle_thread(void) {
    for (;;) {
        __asm__ volatile("sti; hlt");
        task_yield();
    }
}

/* ------------------------------------------------------------------ */
/*  Initialise                                                          */
/* ------------------------------------------------------------------ */

void sched_init(void) {
    num_tasks  = 0;
    current_id = 0;

    /* Task 0 = idle, reusing the boot stack (0x90000) */
    tasks[0].id     = 0;
    tasks[0].state  = TASK_RUNNING;
    tasks[0].kstack = 0x90000;
    for (int i = 0; i < 16; i++) tasks[0].name[i] = 0;
    tasks[0].name[0] = 'i'; tasks[0].name[1] = 'd'; tasks[0].name[2] = 'l';
    tasks[0].name[3] = 'e';
    num_tasks = 1;
    current_task = &tasks[0];

    serial_puts("[sched] init: idle task created\n");
}

/* ------------------------------------------------------------------ */
/*  Create a kernel thread                                              */
/* ------------------------------------------------------------------ */

int task_create(void (*entry)(void), const char *name) {
    if (num_tasks >= MAX_TASKS) return -1;

    /* Allocate 2 pages (8 KiB) for the kernel stack */
    uintptr_t stack_top = pmm_alloc_pages(2);
    if (!stack_top) return -1;

    /* Stack grows down from the top */
    uint32_t *sp = (uint32_t *)(stack_top + 2 * 4096);

    /* Build initial stack frame for switch_context */
    *--sp = (uint32_t)entry;   /* "return address" */
    *--sp = 0x00000202;        /* eflags (IF=1) */
    *--sp = 0;                 /* eax */
    *--sp = 0;                 /* ecx */
    *--sp = 0;                 /* edx */
    *--sp = 0;                 /* ebx */
    *--sp = 0;                 /* ebp */
    *--sp = 0;                 /* esi */
    *--sp = 0;                 /* edi */

    int id = num_tasks++;
    tasks[id].id     = id;
    tasks[id].esp    = (uint32_t)sp;
    tasks[id].kstack = stack_top;
    tasks[id].state  = TASK_READY;
    for (int i = 0; i < 15 && name[i]; i++) tasks[id].name[i] = name[i];
    tasks[id].name[15] = 0;

    serial_puts("[sched] task ");
    serial_puthex(id);
    serial_puts(" '");
    serial_puts(name);
    serial_puts("' created, esp=0x");
    serial_puthex((uint32_t)sp);
    serial_puts("\n");

    return id;
}

/* ------------------------------------------------------------------ */
/*  Round-robin scheduler                                               */
/* ------------------------------------------------------------------ */

static void schedule(void) {
    __asm__ volatile("cli");

    int next = current_id;
    do {
        next = (next + 1) % num_tasks;
    } while (tasks[next].state != TASK_READY && next != current_id);

    if (next == current_id) { __asm__ volatile("sti"); return; }

    struct task *prev = current_task;
    struct task *task = &tasks[next];

    prev->state = TASK_READY;
    task->state = TASK_RUNNING;
    current_id  = next;
    current_task = task;

    switch_context(&prev->esp, (uint32_t *)(uintptr_t)task->esp);

    __asm__ volatile("sti");
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void task_yield(void) {
    schedule();
}

void sched_tick(void) {
    schedule();
}
