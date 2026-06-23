# BuzzOS

BuzzOS 是一个面向学习和实验的极简 i386 POSIX-like 操作系统。它已经不只是启动骨架：当前版本有用户态 shell、多任务、系统调用、VFS、一个简单落盘文件系统、基础 TCP/UDP/ICMP socket、pipe 和 futex 风格同步接口。

English: [README.en.md](README.en.md)

![image-20260623000416138](/pic/demo1.png)

![image-20260623000538562](/pic/demo2.png)

![image-20260623000641242](/pic/demo3.png)

## 当前能力

- 16 位 BIOS 启动扇区，进入 32 位保护模式。
- GDT、IDT、异常处理、PIC、PIT timer、键盘输入、VGA 文本输出、串口输出。
- E820 物理内存探测、bitmap PMM、分页、用户态地址空间。
- ELF32 用户程序加载，用户态 `/bin/sh` shell，支持 Ctrl+C、左右移动光标、Home/End、Delete 和上下翻历史。
- 用户态 `nano` 编辑器和 `basm` 小型汇编器，可在 BuzzOS 内编辑、汇编并运行简单汇编程序。
- 抢占式任务调度，进程/线程模型，`spawn`、`join`、`sleep`、`waitpid`、`kill`。
- 系统调用 ABI：文件、进程、目录、网络、IPC、同步等基础接口。
- VFS + mount table：
  - `/`：initrd/ramfs，包含 `/hello` 和 `/bin/sh`
  - `/dev`：独立 devfs，包含 `console`、`serial`、`null`
  - `/fs`：mini ext-like 落盘文件系统
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
  - `pipe(int fds[2])`
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

只运行已有镜像，不触发重新构建：

```sh
make run-current
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
| LBA 1..384 | kernel 预留区，最多 192 KiB |
| LBA 512..1023 | `/fs` mini 文件系统区域，256 KiB |

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
cat <file>
mkdir <dir>
rmdir <dir>
touch <file>
write <file> <text>
nano <file>
basm <input.asm> [output]
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
wget <host>
dhcp
pipetest
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

- socket 第一版仍复用内核中的单 TCP 连接状态，同时只支持 TCP client、UDP datagram 和 ICMP echo。
- futex 当前是 yield-based wait/wake，接口形状保留，后续应接入真正阻塞队列。
- mini 文件系统是固定大小、直接块 + 一级 indirect block、无日志的教学实现。
- syscall 用户指针校验是范围检查，不是完整页级权限验证。
- 还没有 `fork/execve`、权限模型、动态链接、信号、成熟网络协议栈。

## 推荐下一步

- 把 futex 接入 scheduler wait queue，避免等待时空转。
- 把 TCP 状态从全局变量拆成 per-socket PCB，支持多个并发连接。
- 增加 `fork` / `execve` / `pipe` shell 管道。
- 给 minifs 加 fsck、free list 校验和更完整的 rename/unlink 语义。
- 增加 `/proc` 这类伪文件系统，把任务、内存、网络状态暴露成文件。

## 代码入口

- Bootloader: [src/boot/boot.asm](src/boot/boot.asm)
- Kernel entry: [src/kernel/core/kernel.c](src/kernel/core/kernel.c)
- Scheduler/processes: [src/kernel/sched/task.c](src/kernel/sched/task.c)
- Syscalls: [src/kernel/syscall/syscall.c](src/kernel/syscall/syscall.c)
- VFS core: [src/kernel/fs/vfs.c](src/kernel/fs/vfs.c)
- Filesystem adapters: [src/kernel/fs/ramfs.c](src/kernel/fs/ramfs.c), [src/kernel/fs/devfs.c](src/kernel/fs/devfs.c), [src/kernel/fs/minifs_vfs.c](src/kernel/fs/minifs_vfs.c), [src/kernel/fs/pipefs.c](src/kernel/fs/pipefs.c)
- Mini FS disk format: [src/kernel/fs/minifs/minifs.c](src/kernel/fs/minifs/minifs.c)
- ATA/block cache: [src/kernel/block](src/kernel/block)
- Network stack: [src/kernel/net/net.c](src/kernel/net/net.c)
- User shell: [src/user/bin/shell.c](src/user/bin/shell.c)
- Nano editor: [src/user/bin/nano.c](src/user/bin/nano.c)
- In-OS assembler: [src/user/bin/basm.c](src/user/bin/basm.c)
- User libc: [src/user/libc/libc.c](src/user/libc/libc.c)
- Assembly tutorial: [docs/assembly-programming.md](docs/assembly-programming.md)
