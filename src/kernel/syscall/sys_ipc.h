#ifndef BUZZOS_SYS_IPC_H
#define BUZZOS_SYS_IPC_H

/* Called by the scheduler while interrupts are disabled when a task is being
 * removed without returning through the futex wait syscall path. */
void futex_cancel_task_locked(int task_id);
int futex_status_text(char *buf, int cap);

#endif /* BUZZOS_SYS_IPC_H */
