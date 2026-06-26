# BuzzOS

BuzzOS is a minimal i386 POSIX-like operating system for learning and experimentation. It is no longer just a boot skeleton: the current tree has a user-space shell, multitasking, syscalls, a VFS, a tiny persistent filesystem, basic TCP/UDP/ICMP sockets, pipes, and futex-style synchronization.

Chinese main README: [README.md](README.md)

Project log: [CHANGELOG.md](CHANGELOG.md). Current status and roadmap:
[docs/project-status.md](docs/project-status.md). Run `make report` to generate
the local verification report at `build/project-report.md`.

<table>
  <tr>
    <td><img src="/pic/demo1.png" alt="BuzzOS demo 1"></td>
    <td><img src="/pic/demo2.png" alt="BuzzOS demo 2"></td>
  </tr>
  <tr>
    <td><img src="/pic/demo3.png" alt="BuzzOS demo 3"></td>
    <td><img src="/pic/demo4.png" alt="BuzzOS demo 4"></td>
  </tr>
  <tr>
    <td><img src="/pic/demo5.png" alt="BuzzOS demo 5"></td>
    <td><img src="/pic/demo6.png" alt="BuzzOS demo 6"></td>
  </tr>
</table>

## Current Features

- 16-bit BIOS boot sector and transition to 32-bit protected mode.
- GDT, IDT, exception handling, PIC, PIT timer, keyboard input, VGA text output, and serial output.
- E820 memory detection, bitmap physical memory manager, paging, and user address spaces.
- ELF32 user-program loader and a user-space `/bin/sh` shell with Ctrl+C, left/right cursor movement, Home/End, Delete, up/down command history, multi-stage pipelines, and basic redirection.
- User-space `nano` editor and small `basm` assembler for editing, assembling, and running simple assembly programs inside BuzzOS.
- User-space `gui` desktop using framebuffer blits, PS/2 mouse input, and graphics syscalls. It includes Paint, a built-in shell panel, and a `/fs/apps` GUI app launcher.
- Seeded user GUI apps in `/fs/apps`: `guidemo` with a single-line textbox, `notes` with multiline editing, `forms` with multiple focused text boxes, and `calc` with two focused input boxes, saved state, and `.app` manifest metadata in the App Center.
- Preemptive scheduling, process/thread model, `spawn`, `join`, `sleep`, `waitpid`, and `kill`.
- Syscall ABI for files, processes, directories, networking, IPC, and synchronization.
- VFS with a mount table:
  - `/`: initrd/ramfs, including `/hello`, `/bin/sh`, `/bin/echo`, and `/bin/cat`
  - `/dev`: standalone devfs with `console`, `serial`, and `null`
  - `/fs`: persistent mini ext-like filesystem; user-added GUI apps should live in `/fs/apps`
- Mini filesystem:
  - directories, regular files, `mkdir`, `rmdir`, `unlink`, and `rename`
  - `stat`, `getdents`, `open(O_CREAT/O_TRUNC/O_APPEND)`, and `lseek`
  - fixed disk area, preserved by default across image rebuilds
  - 128 inodes, 382 data blocks, direct blocks plus one indirect block
  - maximum single-file size is about 132 KiB; the `/fs` area is 256 KiB
- Block layer:
  - ATA PIO sector I/O
  - simple write-through block cache
- Networking:
  - NE2000 on QEMU user-mode networking
  - DHCP initialization, DNS, TCP client, UDP datagrams, and ICMP echo
  - user-space socket API: `socket`, `connect`, `send`, `recv`, `bind`, `sendto`, `recvfrom`
- IPC and synchronization:
  - `pipe(int fds[2])` with blocking read/write wakeups
  - shell examples: `echo hello | cat | cat`, `echo saved > /fs/out`, `cat < /fs/out`
  - `futex_wait` / `futex_wake`
- Multi-interface project identity: `/proc/about`, the text-shell `about`
  command, the GUI-shell `about` command, and `make report` expose the same
  compact project introduction and documentation map.
- Multi-interface health surface: `/proc/health`, the text-shell `health`
  command, the GUI-shell `health` command, and `make report` share one compact
  status vocabulary.
- Multi-interface capability matrix: `/proc/interfaces`, text-shell
  `interfaces`, GUI-shell `interfaces`, and `make report` expose stable and
  experimental entrypoints.
- Lightweight runtime limits: `/proc/limits`, text-shell `limits`, GUI-shell
  `limits`, and `make report` expose task, fd, pipe, mount, memory, and minifs
  capacity boundaries without adding a configuration service.

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

Show the local workflow commands:

```sh
make help
```

Check the local build and run environment first:

```sh
make doctor QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

`make doctor` runs `tools/doctor.py` and checks `python`, `make`, PowerShell,
NASM, the LLVM toolchain, and the QEMU path.

Build the image:

```sh
make
```

Build and run:

```sh
make run
```

Run in a visible QEMU window with serial output written to `build/serial-live.log`
instead of stealing the terminal:

```sh
make run-local QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Start directly in the GUI manager or a seeded user GUI demo:

```sh
make run-gui QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
make run-guidemo QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
make run-notes QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
make run-forms QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
make run-calc QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Run the existing image without rebuilding:

```sh
make run-current
```

Run the smoke test:

```sh
make smoke QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Run the fast host-side project consistency check:

```sh
make check-project
```

Check the persistent mini filesystem in the current image:

```sh
make fs-check
make fs-ls
```

Validate only the seeded GUI app manifests and app ELF outputs:

```sh
make app-registry
make app-check
```

Run the GUI smoke test. It boots a copy of the image, drives `forms`, `notes`,
and `guidemo`, validates nonblank screenshots, and writes PNGs under
`build/gui-smoke`:

```sh
make gui-smoke QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Run all current verification checks:

```sh
make verify QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
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
| LBA 1..767 | reserved kernel area, up to 383.5 KiB |
| LBA 768..1279 | `/fs` mini filesystem area, 256 KiB |

`tools/mkimage.ps1` preserves the old `/fs` region by default when rebuilding the image, including layout moves when it can find the minifs superblock. Use `make image-reset-fs` when you want a clean filesystem.

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
about
health
interfaces
limits
fsstat
fdstat
cat <file>
mkdir <dir>
rmdir <dir>
touch <file>
nano <file>
basm <input.asm> [output]
gui
apps [list|info <name>|run <name>]
guidemo
notes
forms
calc
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
wget <host> [port]
tcptwotest <host> <port-a> <port-b>
dhcp
elfbadtest
pipetest
pipeedgetest
pipeblocktest
futextest
```

Edit, assemble, and run assembly inside BuzzOS:

```text
nano /fs/demo.asm
basm /fs/demo.asm /fs/demo
exec /fs/demo
```

In `nano`, `Ctrl+T` inserts a minimal assembly template, `Ctrl+S` saves, and `Ctrl+C` exits.

`basm` is not full NASM. It is a small BuzzOS-focused assembler for learning and experiments. It supports a practical subset such as `bits/global/section/%define/equ/label`, `db/dd`, and `mov/xor/int/ret/nop/push/pop/call/jmp/jcc/add/sub/cmp`, and emits ELF32 files that the BuzzOS loader can execute directly.

Graphics desktop:

```text
gui
```

It starts in APP MANAGER. Select Paint, Shell, or an external GUI program from `/fs/apps`. Paint draws with the mouse, and the GUI Shell supports `help`, `ls`, `cat`, `echo`, `apps`, and `run <path>`. Put ELF32 GUI programs in `/fs/apps/<name>` and launch them by clicking the app entry or running `run /fs/apps/<name>` from the GUI shell. The seeded examples are:

- `guidemo`: buttons, color swatches, a focused single-line textbox, mouse input, and persistent state in `/fs/apps/guidemo.cfg`.
- `notes`: a multiline text editor that saves to `/fs/apps/notes.txt`.
- `forms`: four single-line text boxes with mouse focus, Tab/Enter focus movement, Left/Right/Home/End/Delete editing, live preview, and persistent state in `/fs/apps/forms.cfg`.
- `calc`: two single-line input boxes, operation buttons, keyboard editing, result feedback, and persistent state in `/fs/apps/calc.cfg`.

Each user GUI app has source-side metadata files such as
`src/user/bin/forms.app`, `src/user/bin/forms.readme`, and optional
`src/user/bin/forms.seed`. `tools/gen_app_registry.py` generates
`src/kernel/app_registry.h` from those files so the kernel seeds `/fs/apps`
from a compact data table. The App Center reads the `.app` manifest for its
detail panel while still launching the ELF executable from disk.

Create a small app scaffold on the host:

```sh
make new-app APP=todo
make app-registry
python tools/check_project.py --list-apps
```

The text shell can inspect and launch the same app model:

```text
apps
apps info forms
apps run forms
```

In an app, `Esc` or `Ctrl+C` returns to the manager; from the manager it returns to the text shell.

The current GUI is a user-space full-screen app manager, not a complete window system. The kernel only exposes VGA 13h graphics mode, framebuffer blits, PS/2 mouse state, and graphics syscalls; layout, Paint, the GUI Shell, external app launching, and the mouse cursor are implemented by the `/bin/gui` user program. This keeps the kernel small and gives BuzzOS a clear path toward a real GUI server later.

Filesystem test example:

```text
fsstat
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

- Stream sockets now keep per-socket TCP PCBs with lightweight input demux and small receive buffers. The TCP/IP stack is still a lightweight client implementation, with limited timeout, retransmission, and window behavior.
- Futex wait/wake is scheduler-backed; `/proc/sync` and `futexblocktest` expose blocked waiter state.
- The mini filesystem is fixed-size, uses direct blocks plus one indirect block, and has no journal.
- The GUI is currently a 320x200x256 full-screen user-space manager with Paint, a GUI Shell, mouse input, and `/fs/apps` app launching. It is not a concurrent multi-window desktop yet.
- Syscall user-pointer validation is range-based, not full page-permission validation.
- There is no `fork` / `execve`, permission model, dynamic linker, signal system, or mature network stack yet.

## Recommended Next Steps

- Add stronger TCP socket regression coverage, timeout, retransmission, window, and larger receive-flow behavior.
- Add `fork`, `execve`, shell quoting/env expansion, and job-control polish.
- Add fsck-style validation and stronger rename/unlink semantics to minifs.
- Expand `/proc` with deeper socket, fd flag, filesystem health, and scheduler statistics.

## Code Map

- Bootloader: [src/boot/boot.asm](src/boot/boot.asm)
- Kernel entry: [src/kernel/core/kernel.c](src/kernel/core/kernel.c)
- Scheduler/processes: [src/kernel/sched/task.c](src/kernel/sched/task.c)
- Syscalls: [src/kernel/syscall/syscall.c](src/kernel/syscall/syscall.c)
- Graphics syscall: [src/kernel/syscall/sys_gfx.c](src/kernel/syscall/sys_gfx.c)
- VFS core: [src/kernel/fs/vfs.c](src/kernel/fs/vfs.c)
- Filesystem adapters: [src/kernel/fs/ramfs.c](src/kernel/fs/ramfs.c), [src/kernel/fs/devfs.c](src/kernel/fs/devfs.c), [src/kernel/fs/minifs_vfs.c](src/kernel/fs/minifs_vfs.c), [src/kernel/fs/pipefs.c](src/kernel/fs/pipefs.c)
- Mini FS disk format: [src/kernel/fs/minifs/minifs.c](src/kernel/fs/minifs/minifs.c)
- ATA/block cache: [src/kernel/block](src/kernel/block)
- Network stack: [src/kernel/net/net.c](src/kernel/net/net.c)
- VGA/text/graphics driver: [src/kernel/drv/vga.c](src/kernel/drv/vga.c)
- User shell: [src/user/bin/shell.c](src/user/bin/shell.c)
- GUI desktop: [src/user/bin/gui.c](src/user/bin/gui.c)
- Nano editor: [src/user/bin/nano.c](src/user/bin/nano.c)
- In-OS assembler: [src/user/bin/basm.c](src/user/bin/basm.c)
- User libc: [src/user/libc/libc.c](src/user/libc/libc.c)
- Assembly tutorial: [docs/assembly-programming.md](docs/assembly-programming.md)
