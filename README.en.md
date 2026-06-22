# BuzzOS

BuzzOS is a minimal i386 POSIX-like operating system for learning and experimentation. It is no longer just a boot skeleton: the current tree has a user-space shell, multitasking, syscalls, a VFS, a tiny persistent filesystem, basic TCP/UDP/ICMP sockets, pipes, and futex-style synchronization.

Chinese main README: [README.md](README.md)

![image-20260623000416138](.\pic\demo1.png)

![image-20260623000538562](.\pic\demo2.png)

![image-20260623000641242](.\pic\demo3.png)

## Current Features

- 16-bit BIOS boot sector and transition to 32-bit protected mode.
- GDT, IDT, exception handling, PIC, PIT timer, keyboard input, VGA text output, and serial output.
- E820 memory detection, bitmap physical memory manager, paging, and user address spaces.
- ELF32 user-program loader and a user-space `/bin/sh` shell.
- Preemptive scheduling, process/thread model, `spawn`, `join`, `sleep`, `waitpid`, and `kill`.
- Syscall ABI for files, processes, directories, networking, IPC, and synchronization.
- VFS with a mount table:
  - `/`: initrd/ramfs, including `/hello` and `/bin/sh`
  - `/dev`: `console`, `serial`, and `null`
  - `/fs`: persistent mini ext-like filesystem
- Mini filesystem:
  - directories, regular files, `mkdir`, `rmdir`, `unlink`, and `rename`
  - `stat`, `getdents`, `open(O_CREAT/O_TRUNC/O_APPEND)`, and `lseek`
  - fixed disk area, preserved by default across image rebuilds
- Block layer:
  - ATA PIO sector I/O
  - simple write-through block cache
- Networking:
  - NE2000 on QEMU user-mode networking
  - DHCP initialization, DNS, TCP client, UDP datagrams, and ICMP echo
  - user-space socket API: `socket`, `connect`, `send`, `recv`, `bind`, `sendto`, `recvfrom`
- IPC and synchronization:
  - `pipe(int fds[2])`
  - `futex_wait` / `futex_wake`

## Build And Run

Required tools:

| Tool | Purpose |
| --- | --- |
| `nasm` | Assembles the bootloader and kernel assembly |
| `clang` | Compiles the freestanding kernel and user programs |
| `ld.lld` | Links the kernel and ELF user programs |
| `llvm-objcopy` | Produces the raw kernel binary |
| `make` | Build entry point |
| `powershell` | Image packing on Windows |
| `qemu-system-i386` | Runs BuzzOS |

Build the image:

```sh
make
```

Build and run:

```sh
make run
```

Run the existing image without rebuilding:

```sh
make run-current
```

Rebuild the image and reset the `/fs` filesystem area:

```sh
make image-reset-fs
```

If `make` reports that `build/buzzos.img` or `build/user/*.o` is in use, QEMU is usually still running. Close QEMU and build again.

## Disk Image Layout

`build/buzzos.img` is a raw disk image:

| Area | Purpose |
| --- | --- |
| LBA 0 | boot sector |
| LBA 1..256 | reserved kernel area, up to 128 KiB |
| LBA 512..767 | `/fs` mini filesystem area, 128 KiB |

`tools/mkimage.ps1` preserves the old `/fs` region by default when rebuilding the image. Use `make image-reset-fs` when you want a clean filesystem.

## Shell Commands

After boot, BuzzOS enters the user-space shell:

```text
=== BuzzOS User Shell ===
buzzos:/>
```

Common commands:

```text
ls [path]
cd [path]
pwd
stat <path>
cat <file>
mkdir <dir>
rmdir <dir>
touch <file>
write <file> <text>
rm <file>
mv <old> <new>
exec <program> [args...] [&|bg]
wait [pid]
kill <pid>
ps [-a]
echo <text>
sleep <seconds>
reboot
```

Network and IPC test commands:

```text
ping <host-or-ip>
wget <host>
dhcp
pipetest
futextest
```

Filesystem test example:

```text
mkdir /fs/a
write /fs/a/t hello
cat /fs/a/t
stat /fs/a/t
mv /fs/a/t /fs/a/u
rm /fs/a/u
rmdir /fs/a
```

## Design Boundaries

BuzzOS is intentionally small but structured to grow. It is not a complete Unix yet:

- The first socket layer still reuses a single global TCP connection state. It supports TCP client sockets, UDP datagrams, and ICMP echo.
- Futex wait/wake currently uses yield-based waiting. The interface is in place, but it should move to scheduler-backed wait queues.
- The mini filesystem is fixed-size, direct-block-only, and has no journal.
- Syscall user-pointer validation is range-based, not full page-permission validation.
- There is no `fork` / `execve`, permission model, dynamic linker, signal system, or mature network stack yet.

## Recommended Next Steps

- Move futex wait/wake onto scheduler wait queues to avoid spinning.
- Split TCP state into per-socket PCBs and support concurrent TCP sockets.
- Add `fork`, `execve`, and shell pipelines using `pipe`.
- Move `ramfs`, `devfs`, and `minifs` out of `vfs.c` into separate filesystem modules.
- Add fsck-style validation and stronger rename/unlink semantics to minifs.

## Code Map

- Bootloader: [src/boot/boot.asm](src/boot/boot.asm)
- Kernel entry: [src/kernel/kernel.c](src/kernel/kernel.c)
- Scheduler/processes: [src/kernel/task.c](src/kernel/task.c)
- Syscalls: [src/kernel/syscall.c](src/kernel/syscall.c)
- VFS: [src/kernel/vfs.c](src/kernel/vfs.c)
- Mini FS: [src/kernel/fs/minifs.c](src/kernel/fs/minifs.c)
- ATA/block cache: [src/kernel/block](src/kernel/block)
- Network stack: [src/kernel/net.c](src/kernel/net.c)
- User shell: [src/user/shell.c](src/user/shell.c)
- User libc: [src/user/libc.c](src/user/libc.c)
