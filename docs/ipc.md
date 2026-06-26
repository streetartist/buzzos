# IPC And Synchronization

BuzzOS exposes two small IPC primitives to user space:

```text
pipe(int fds[2])
futex_wait(int *addr, int expected)
futex_wait_timeout(int *addr, int expected, unsigned int timeout_ms)
futex_wake(int *addr, int count)
```

## Pipe Semantics

Pipes are file descriptors backed by the VFS. The current implementation uses
a fixed 512-byte ring buffer and scheduler wakeups for the blocking cases:

- Reading an empty pipe while a writer is still open blocks until data arrives
  or all writers close.
- Reading an empty pipe after all writers close returns `0` for EOF.
- Writing after all readers close returns `-1`.
- Writing to a full pipe while a reader is still open blocks until buffer space
  is available or all readers close.
- A write may still return a short count if readers disappear after some bytes
  were accepted.

The shell command `pipetest` checks the normal write/read path.
The shell command `pipeedgetest` checks EOF and closed-reader behavior.
The shell command `pipeblocktest` checks reader wakeup and full-buffer writer
wakeup behavior.

## Futex Semantics

`futex_wait(addr, expected)` verifies that `*addr == expected`, registers the
current task as a waiter, and blocks it in the scheduler. `futex_wake(addr,
count)` wakes matching blocked tasks.

`futex_wait_timeout(addr, expected, timeout_ms)` uses the same waiter table but
also gives the blocked task a scheduler deadline. It returns `0` when the value
changes or a wake arrives, and `-2` when the deadline expires.

When a task is killed or exits without returning through the futex wait path,
the kernel cancels any waiter slot owned by that task so future waits cannot
lose capacity to dead tasks.

The shell command `futextest` checks a basic wait/wake round trip. The shell
command `futextimeouttest` checks both timeout and wake-before-timeout paths.
The shell command `futexcanceltest` repeatedly kills processes with blocked
futex waiters and confirms the waiter table is reusable afterwards.
The shell command `futexblocktest` leaves a thread blocked long enough to
observe it in `/proc/threads` and `/proc/sync`, then wakes and joins it.
The shell command `syncstat` prints the current `/proc/sync` view.
