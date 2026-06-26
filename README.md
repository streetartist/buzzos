# BuzzOS

BuzzOS 是一个面向学习和实验的极简 i386 POSIX-like 操作系统。它已经不只是启动骨架：当前版本有用户态 shell、多任务、系统调用、VFS、一个简单落盘文件系统、基础 TCP/UDP/ICMP socket、pipe 和 futex 风格同步接口。

English: [README.en.md](README.en.md)

项目日志：[CHANGELOG.md](CHANGELOG.md)。当前状态与路线图：[docs/project-status.md](docs/project-status.md)。运行 `make report` 可生成本地验证报告 `build/project-report.md`。

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

## 当前能力

- 16 位 BIOS 启动扇区，进入 32 位保护模式。
- GDT、IDT、异常处理、PIC、PIT timer、键盘输入、VGA 文本输出、串口输出。
- E820 物理内存探测、bitmap PMM、分页、用户态地址空间。
- ELF32 用户程序加载，用户态 `/bin/sh` shell，支持 Ctrl+C、左右移动光标、Home/End、Delete、上下翻历史、多段 shell 管道和基础重定向。
- 用户态 `nano` 编辑器和 `basm` 小型汇编器，可在 BuzzOS 内编辑、汇编并运行简单汇编程序。
- 用户态 `gui` 桌面，通过 framebuffer blit、PS/2 鼠标和图形 syscall 提供 paint、内置 shell 与 `/fs/apps` GUI 程序启动器。
- `/fs/apps` 默认种子用户 GUI：`guidemo` 单行文本框、`notes` 多行编辑器、`forms` 多文本框表单、持久化状态和 App Center `.app` 元数据。
- 抢占式任务调度，进程/线程模型，`spawn`、`join`、`sleep`、`waitpid`、`kill`。
- 系统调用 ABI：文件、进程、目录、网络、IPC、同步等基础接口。
- VFS + mount table：
  - `/`：initrd/ramfs，包含 `/hello`、`/bin/sh`、`/bin/echo` 和 `/bin/cat`
  - `/dev`：独立 devfs，包含 `console`、`serial`、`null`
  - `/fs`：mini ext-like 落盘文件系统，用户 GUI 程序推荐放在 `/fs/apps`
- mini 文件系统：
  - 目录、普通文件、`mkdir`、`rmdir`、`unlink`、`rename`
  - `stat`、`getdents`、`open(O_CREAT/O_TRUNC/O_APPEND)`、`lseek`
  - 固定磁盘区域，默认重建镜像时保留 `/fs`
  - 128 个 inode，382 个数据块，直接块 + 一级 indirect block
  - 单文件理论上限约 132 KiB，`/fs` 区域总大小 256 KiB
- 块设备层：
  - ATA PIO 扇区读写
  - 简单 write-through block cache
- 网络：
  - NE2000/QEMU user-mode network
  - DHCP 初始化、DNS、TCP client、UDP datagram、ICMP echo
  - 用户态 socket API：`socket/connect/send/recv/bind/sendto/recvfrom`
- IPC/同步：
  - `pipe(int fds[2])`，支持阻塞读写唤醒
  - shell 示例：`echo hello | cat | cat`、`echo saved > /fs/out`、`cat < /fs/out`
  - `futex_wait` / `futex_wake`

## 构建与运行

需要这些工具：

| 工具 | 用途 |
| --- | --- |
| `nasm` | 汇编 bootloader 和内核汇编 |
| `clang` | 编译 freestanding C 内核和用户程序 |
| `ld.lld` | 链接内核和 ELF 用户程序 |
| `llvm-objcopy` | 生成 raw kernel binary |
| `make` | 构建入口 |
| `powershell` | Windows 下打包镜像 |
| `qemu-system-i386` | 运行 BuzzOS |

构建镜像：

```sh
make
```

构建并运行：

```sh
make run
```

在可见 QEMU 窗口中运行，并把串口日志写到 `build/serial-live.log`，避免终端串口占住输入：

```sh
make run-local QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

直接启动到表单输入 GUI 示例：

```sh
make run-forms QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

只运行已有镜像，不触发重新构建：

```sh
make run-current
```

运行 smoke 测试：

```sh
make smoke QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

运行快速项目一致性检查：

```sh
make check-project
```

运行 GUI smoke 测试。它会启动镜像副本，自动操作 `forms`、`notes`、`guidemo`，检查截图不是空白，并把 PNG 写到 `build/gui-smoke`：

```sh
make gui-smoke QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

运行当前全部验证：

```sh
make verify QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

重建镜像并清空 `/fs` 文件系统区域：

```sh
make image-reset-fs
```

如果 `make` 报 `build/buzzos.img` 或 `build/user/*.o` 被占用，通常是 QEMU 还在运行。关掉 QEMU 后再构建。

## 镜像布局

`build/buzzos.img` 是一块 raw 磁盘镜像：

| 区域 | 用途 |
| --- | --- |
| LBA 0 | boot sector |
| LBA 1..767 | kernel 预留区，最多 383 KiB |
| LBA 768..1279 | `/fs` mini 文件系统区域，256 KiB |

`tools/mkimage.ps1` 默认会在重建镜像时保留旧镜像的 `/fs` 区域。需要清空时显式运行 `make image-reset-fs`。

## Shell 命令

启动后会进入用户态 shell：

```text
=== BuzzOS User Shell ===
buzzos:/>
```

常用命令：

```text
ls [path]
cd [path]
pwd
stat <path>
fsstat
fdstat
cat <file>
mkdir <dir>
rmdir <dir>
touch <file>
write <file> <text>
nano <file>
basm <input.asm> [output]
gui
apps [list|info <name>|run <name>]
guidemo
notes
forms
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

网络和 IPC 测试：

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

在 BuzzOS 内写汇编并运行：

```text
nano /fs/demo.asm
basm /fs/demo.asm /fs/demo
exec /fs/demo
```

`nano` 里可以按 `Ctrl+T` 插入一个最小汇编模板，`Ctrl+S` 保存，`Ctrl+C` 退出。

`basm` 不是完整 NASM，而是面向 BuzzOS 教学和实验的小型 assembler。它支持 `bits/global/section/%define/equ/label`、`db/dd`、`mov/xor/int/ret/nop/push/pop/call/jmp/jcc/add/sub/cmp` 等常用子集，输出的是 BuzzOS loader 可直接执行的 ELF32 文件。

图形桌面：

```text
gui
```

进入后先显示 APP MANAGER，由用户选择 Paint、Shell 或 `/fs/apps` 里的外部 GUI 程序。Paint 用鼠标绘图；Shell 支持 `help`、`ls`、`cat`、`echo`、`apps`、`run <path>` 等内置命令。把 ELF32 GUI 程序放到 `/fs/apps/<name>` 后，可在管理器内点击或在 GUI Shell 里用 `run /fs/apps/<name>` 启动；外部 GUI 程序退出后会回到管理器。

当前内置的用户态 GUI 示例包括：

- `guidemo`：按钮、色块、单行文本框、鼠标输入和 `/fs/apps/guidemo.cfg` 持久化状态。
- `notes`：多行文本编辑器，保存到 `/fs/apps/notes.txt`。
- `forms`：四个单行文本框，支持鼠标聚焦、Tab/Enter 切换、Left/Right/Home/End/Delete 编辑、实时预览和 `/fs/apps/forms.cfg` 持久化状态。

每个用户 GUI app 可以在可执行文件旁放一个 `.app` manifest，使用简单的 `key=value` 字段，例如 `name`、`kind`、`version`、`summary`、`state`、`source`。App Center 会读取这些字段显示详情，同时仍然从磁盘启动 ELF 可执行文件。

文本 shell 也可以查看和启动同一套 app 模型：

```text
apps
apps info forms
apps run forms
```

应用内按 `Esc` 或 `Ctrl+C` 返回管理器，管理器内再按一次返回文本 shell。

当前 GUI 是一个用户态全屏 app manager，不是完整窗口系统。内核只提供 VGA 13h 图形模式、framebuffer blit、PS/2 鼠标状态和图形 syscall；界面布局、绘画程序、GUI Shell、外部 app 启动和鼠标光标都在 `/bin/gui` 用户程序里实现。这样能保持内核小而稳定，也方便后续把它演进成真正的 GUI server。

文件系统测试示例：

```text
mkdir /fs/a
write /fs/a/t hello
cat /fs/a/t
stat /fs/a/t
mv /fs/a/t /fs/a/u
rm /fs/a/u
rmdir /fs/a
```

## 设计边界

BuzzOS 当前是“小而可扩展”的实现，不是完整 Unix：

- TCP stream socket 已经使用 per-socket PCB、轻量输入 demux 和小接收缓冲；网络栈仍是轻量 client 实现，超时、重传和窗口能力还有限。
- futex wait/wake 已接入 scheduler 阻塞与唤醒；`/proc/sync` 和 `futexblocktest` 可观察等待状态。
- mini 文件系统是固定大小、直接块 + 一级 indirect block、无日志的教学实现。
- GUI 当前是 320x200x256 的全屏用户态管理器，支持 Paint、GUI Shell、鼠标和 `/fs/apps` app 启动；它还不是并发多窗口桌面。
- syscall 用户指针校验是范围检查，不是完整页级权限验证。
- 还没有 `fork/execve`、权限模型、动态链接、信号、成熟网络协议栈。

## 推荐下一步

- 增加更多 TCP socket 回归测试，并继续完善超时、重传、窗口和更大的接收流场景。
- 增加 `fork` / `execve` / shell 引号与环境变量展开 / job control 打磨。
- 给 minifs 加 fsck、free list 校验和更完整的 rename/unlink 语义。
- 继续扩展 `/proc`，补齐更细的 socket、fd flag、文件系统健康度和调度统计。

## 代码入口

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
