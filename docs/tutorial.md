# BuzzOS 手把手教程

这份教程面向第一次打开 BuzzOS 仓库的人。目标不是只告诉你“怎么运行”，而是让你能沿着一条完整路径理解它：

```text
BIOS -> boot sector -> 32-bit kernel -> user shell -> VFS/minifs -> network/socket -> IPC/sync -> GUI desktop
```

读法建议：

- 第一次看：从第 0 章开始按顺序做，先把系统跑起来。
- 想改功能：直接跳到对应章节，看源码入口和验证命令。
- 出问题：先看第 19 章，里面列了常见错误和定位方式。

BuzzOS 现在已经不是早期的“只会启动并写 VGA 字符”的骨架。当前版本有用户态 shell、多任务、系统调用、VFS、一个简单落盘文件系统、基础网络 socket、pipe、futex 风格同步接口，以及可从 shell 启动的图形桌面。

---

## 目录

- [0. 当前 BuzzOS 能做什么](#0-当前-buzzos-能做什么)
- [1. 开发环境准备](#1-开发环境准备)
- [2. 仓库地图](#2-仓库地图)
- [3. 构建镜像](#3-构建镜像)
- [4. 运行 BuzzOS](#4-运行-buzzos)
- [5. 第一次进入 shell](#5-第一次进入-shell)
- [6. 镜像布局和持久化文件区](#6-镜像布局和持久化文件区)
- [7. 文件系统实验](#7-文件系统实验)
- [8. 进程、线程和后台任务实验](#8-进程线程和后台任务实验)
- [9. 网络实验](#9-网络实验)
- [10. IPC 和同步实验](#10-ipc-和同步实验)
- [11. 启动链：BIOS 到 C 内核](#11-启动链bios-到-c-内核)
- [12. 内核初始化顺序](#12-内核初始化顺序)
- [13. 中断、异常和基础驱动](#13-中断异常和基础驱动)
- [14. 内存管理和用户态地址空间](#14-内存管理和用户态地址空间)
- [15. ELF 用户程序、libc 和 syscall ABI](#15-elf-用户程序libc-和-syscall-abi)
- [16. VFS、mount table 和 minifs](#16-vfsmount-table-和-minifs)
- [17. 网络栈和用户态 socket](#17-网络栈和用户态-socket)
- [18. 图形桌面、framebuffer 和 GUI app](#18-图形桌面framebuffer-和-gui-app)
- [19. 常见问题排查](#19-常见问题排查)
- [20. 继续扩展 BuzzOS](#20-继续扩展-buzzos)

---

## 0. 当前 BuzzOS 能做什么

BuzzOS 是一个教学型、极简但可扩展的 i386 POSIX-like OS。它的定位是：代码量小到可以读完，但结构尽量朝真实 Unix/Linux 的方向靠。

当前能力：

- BIOS 启动扇区，进入 32 位保护模式。
- GDT、IDT、异常处理、PIC、PIT timer、键盘、VGA 文本输出、串口输出。
- E820 物理内存探测、bitmap PMM、分页、用户态地址空间。
- ELF32 用户程序加载。
- 用户态 shell：`/bin/sh`，支持行内光标移动和命令历史。
- 用户态程序：`nano`、`basm`、`gui`。
- VGA 320x200x256 图形模式和基础绘图 syscall。
- 抢占式调度，进程/线程模型。
- 系统调用：文件、目录、进程、线程、网络、IPC、同步等基础接口。
- VFS + mount table：
  - `/`：initrd/ramfs，内置 `/hello` 和 `/bin/sh`
  - `/dev`：独立 devfs，包含 `console`、`serial`、`null`
  - `/fs`：mini ext-like 落盘文件系统
- minifs：目录、普通文件、`mkdir/rmdir/unlink/rename/stat/getdents/open/lseek`。
- ATA PIO + 简单 write-through block cache。
- 网络：NE2000、DHCP、DNS、TCP client、UDP datagram、ICMP echo。
- 用户态 socket API：`socket/connect/send/recv/bind/sendto/recvfrom`。
- IPC/同步：`pipe(int fds[2])`、`futex_wait/futex_wake`。

当前边界也要清楚：

- 还没有完整 POSIX：没有 `fork`、`execve`、信号、权限模型、动态链接。
- TCP stream socket 已经有 per-socket PCB、输入 demux 和小接收缓冲，但超时、重传和窗口仍是轻量实现。
- futex wait/wake 已接入 scheduler 阻塞与唤醒，`/proc/sync` 可观察等待槽。
- minifs 是固定大小、直接块 + 一级 indirect block、无日志的教学文件系统。
- syscall 用户指针校验是范围检查，还不是完整的页级权限校验。

---

## 1. 开发环境准备

BuzzOS 的默认开发环境是 Windows + PowerShell + Make + LLVM + QEMU。Linux/macOS 也能移植构建，但当前仓库脚本优先照顾 Windows。

### 1.1 必需工具

| 工具 | 用途 |
| --- | --- |
| `nasm` | 汇编 bootloader 和内核汇编文件 |
| `clang` | 编译 freestanding C 内核和用户程序 |
| `ld.lld` | 链接内核和 ELF 用户程序 |
| `llvm-objcopy` | 把 kernel ELF 转成 raw binary |
| `make` | 调用 Makefile |
| `powershell` | 运行 `tools/mkimage.ps1` |
| `python` | 运行 `tools/mkinitrd.py` |
| `qemu-system-i386` | 启动 BuzzOS |

Windows 上可以用 Chocolatey 安装：

```powershell
choco install -y nasm llvm make qemu python
```

装完后新开 PowerShell，确认命令能找到：

```powershell
nasm -v
clang --version
ld.lld --version
llvm-objcopy --version
make --version
python --version
qemu-system-i386 --version
```

如果某个命令提示找不到，先检查 PATH。BuzzOS 的构建本身不依赖 IDE。

### 1.2 进入仓库

```powershell
cd D:\Project\buzzos
```

后面教程里的所有命令默认都在这个目录执行。

---

## 2. 仓库地图

先知道文件在哪里，后面读源码会轻很多。

| 路径 | 作用 |
| --- | --- |
| [Makefile](../Makefile) | 构建入口，编译 boot/kernel/user，生成镜像，启动 QEMU |
| [linker.ld](../linker.ld) | 内核链接脚本 |
| [tools/mkimage.ps1](../tools/mkimage.ps1) | 生成 `build/buzzos.img`，保留或重置 `/fs` 区域 |
| [tools/mkinitrd.py](../tools/mkinitrd.py) | 把用户程序打进 initrd header |
| [src/boot/boot.asm](../src/boot/boot.asm) | 512 字节 boot sector，读内核并进保护模式 |
| [src/kernel/core/kernel.c](../src/kernel/core/kernel.c) | C 内核入口 `_start` |
| [src/kernel/arch/i386/gdt.c](../src/kernel/arch/i386/gdt.c) | GDT/TSS |
| [src/kernel/arch/i386/idt.c](../src/kernel/arch/i386/idt.c) | IDT、异常和中断入口 |
| [src/kernel/mm/pmm.c](../src/kernel/mm/pmm.c) | 物理页分配器 |
| [src/kernel/arch/i386/paging.c](../src/kernel/arch/i386/paging.c) | 分页和用户地址空间 |
| [src/kernel/sched/task.c](../src/kernel/sched/task.c) | task/process/thread/scheduler |
| [src/kernel/core/exec.c](../src/kernel/core/exec.c) | ELF 用户程序启动 |
| [src/kernel/syscall/syscall.c](../src/kernel/syscall/syscall.c) | syscall 入口、dispatch、用户指针校验 |
| [src/kernel/syscall/sys_file.c](../src/kernel/syscall/sys_file.c) | 文件和目录 syscall |
| [src/kernel/syscall/sys_proc.c](../src/kernel/syscall/sys_proc.c) | 进程、线程、调度相关 syscall |
| [src/kernel/syscall/sys_net.c](../src/kernel/syscall/sys_net.c) | socket/network syscall |
| [src/kernel/syscall/sys_ipc.c](../src/kernel/syscall/sys_ipc.c) | pipe、futex 等 IPC/同步 syscall |
| [src/kernel/syscall/sys_gfx.c](../src/kernel/syscall/sys_gfx.c) | VGA 图形 syscall |
| [src/kernel/fs/vfs.c](../src/kernel/fs/vfs.c) | VFS core：路径、mount table、fd/open file |
| [src/kernel/fs/ramfs.c](../src/kernel/fs/ramfs.c) | `/` initrd/ramfs adapter |
| [src/kernel/fs/devfs.c](../src/kernel/fs/devfs.c) | `/dev` 字符设备文件系统 |
| [src/kernel/fs/pipefs.c](../src/kernel/fs/pipefs.c) | pipe fd 实现 |
| [src/kernel/fs/minifs_vfs.c](../src/kernel/fs/minifs_vfs.c) | minifs 接入 VFS 的 adapter |
| [src/kernel/fs/minifs/minifs.c](../src/kernel/fs/minifs/minifs.c) | `/fs` 落盘文件系统 |
| [src/kernel/block/ata.c](../src/kernel/block/ata.c) | ATA PIO 扇区读写 |
| [src/kernel/block/cache.c](../src/kernel/block/cache.c) | 简单块缓存 |
| [src/kernel/net/net.c](../src/kernel/net/net.c) | ARP/IP/ICMP/UDP/TCP/DNS/DHCP |
| [src/kernel/drv/ne2000.c](../src/kernel/drv/ne2000.c) | NE2000 网卡驱动 |
| [src/kernel/drv/vga.c](../src/kernel/drv/vga.c) | 文本 console 和 VGA 图形模式 |
| [src/user/libc/crt0.c](../src/user/libc/crt0.c) | 用户程序入口 |
| [src/user/libc/libc.c](../src/user/libc/libc.c) | 用户态 syscall wrapper 和小 libc |
| [src/user/libc/libc.h](../src/user/libc/libc.h) | 用户态 API 声明 |
| [src/user/bin/shell.c](../src/user/bin/shell.c) | 用户态 shell |
| [src/user/bin/nano.c](../src/user/bin/nano.c) | 用户态编辑器 |
| [src/user/bin/basm.c](../src/user/bin/basm.c) | BuzzOS 内置小型 assembler |
| [src/user/bin/gui.c](../src/user/bin/gui.c) | 图形界面 demo |
| [src/user/bin/hello.c](../src/user/bin/hello.c) | 多线程 demo 用户程序 |

最重要的阅读顺序：

```text
Makefile
src/boot/boot.asm
src/kernel/core/kernel.c
src/kernel/sched/task.c
src/kernel/syscall/syscall.c
src/kernel/fs/vfs.c
src/user/libc/libc.c
src/user/bin/shell.c
```

---

## 3. 构建镜像

直接运行：

```powershell
make
```

构建完成后会生成：

```text
build/buzzos.img
```

这是一个 raw 磁盘镜像。QEMU 会把它当成一块硬盘启动。

### 3.1 Makefile 做了什么

构建流程可以拆成 6 步：

1. `nasm -f bin src/boot/boot.asm` 生成 boot sector。
2. `clang --target=i386-none-elf` 编译内核 C 文件。
3. `nasm -f elf32` 编译内核汇编文件。
4. `ld.lld -m elf_i386 -T linker.ld` 链接内核 ELF。
5. `llvm-objcopy -O binary` 生成 raw `kernel.bin`。
6. `tools/mkimage.ps1` 把 boot sector、kernel 和 `/fs` 区域拼成 `buzzos.img`。

用户程序也在这个流程里构建：

1. 编译 `src/user/libc/crt0.c`、`src/user/libc/libc.c`。
2. 编译 `src/user/bin/hello.c`、`shell.c`、`nano.c`、`basm.c`、`gui.c`、`echo.c` 和 `cat.c`。
3. 链接成 ELF32 用户程序。
4. `tools/mkinitrd.py` 把 `/hello`、`/bin/sh`、`/bin/nano`、`/bin/basm`、`/bin/gui`、`/bin/echo` 和 `/bin/cat` 写进 `src/kernel/initrd.h`。
5. 内核构建时把 initrd 作为静态数组编进 kernel。

### 3.2 常用 make 目标

| 命令 | 作用 |
| --- | --- |
| `make` | 构建镜像 |
| `make run` | 构建并运行 |
| `make run-current` | 不重新构建，直接运行已有 `build/buzzos.img` |
| `make image-reset-fs` | 重建镜像并清空 `/fs` 持久化区域 |
| `make clean` | 删除 `build` |

平时调内核或用户程序用：

```powershell
make run
```

只是想重启当前镜像、保留 build 结果时用：

```powershell
make run-current
```

想清空 `/fs` 测试干净环境时用：

```powershell
make image-reset-fs
make run-current
```

---

## 4. 运行 BuzzOS

最简单：

```powershell
make run
```

Makefile 里的 QEMU 命令等价于：

```powershell
qemu-system-i386 `
  -drive format=raw,file=build\buzzos.img `
  -serial stdio `
  -no-reboot `
  -netdev user,id=n0 `
  -device ne2k_isa,netdev=n0,iobase=0x300,irq=10
```

参数含义：

| 参数 | 意义 |
| --- | --- |
| `-drive format=raw,file=build\buzzos.img` | 把镜像作为硬盘 |
| `-serial stdio` | 串口输出接到当前终端 |
| `-no-reboot` | 崩溃后不要自动重启，方便看异常 |
| `-netdev user,id=n0` | QEMU user-mode network |
| `-device ne2k_isa,...` | 给 BuzzOS 一块 NE2000 ISA 网卡 |

如果你只看到了 QEMU 窗口没有终端日志，确认用了 `-serial stdio`。

---

## 5. 第一次进入 shell

启动后你应该看到类似：

```text
=== BuzzOS User Shell ===

buzzos:/>
```

输入：

```text
help
```

当前 shell 支持：

```text
ls cd pwd stat fsstat fdstat cat mkdir rmdir touch write rm mv nano basm gui ping wget tcptwotest dhcp elfbadtest pipetest futextest exec wait kill ps echo sleep reboot help
```

先做最小验证：

```text
pwd
ls /
ls /dev
ls /fs
echo hello
```

预期：

- `pwd` 输出 `/`。
- `ls /` 能看到内置文件和目录，比如 `hello` 是文件不带斜杠，`bin/` 是目录会带斜杠。
- `ls /dev` 能看到 `console`、`serial`、`null`。
- `ls /fs` 访问的是落盘 minifs 根目录。

shell 的提示符带当前目录：

```text
buzzos:/>
buzzos:/fs>
```

内置 `nano` 和 `basm` 可以直接在 BuzzOS 里写一个简单汇编程序、汇编成 ELF，然后运行：

```text
nano /fs/demo.asm
basm /fs/demo.asm /fs/demo
exec /fs/demo
```

`nano` 里 `Ctrl+T` 插入最小汇编模板，`Ctrl+S` 保存，`Ctrl+C` 退出。

也可以启动图形桌面：

```text
gui
```

进入 GUI 后先看到 APP MANAGER，用户选择 Paint、Shell 或 `/fs/apps` 里的外部 GUI 程序后才打开应用。Paint 可用鼠标绘图；GUI Shell 支持 `help`、`ls`、`cat`、`echo`、`apps` 和 `run <path>`；用户添加的 ELF32 GUI 程序放到 `/fs/apps` 后即可点击或 `run /fs/apps/<name>` 启动。应用内 `Esc` 或 `Ctrl+C` 返回管理器，管理器内再按一次返回文本 shell。

`cd` 示例：

```text
cd /fs
pwd
cd /
pwd
```

---

## 6. 镜像布局和持久化文件区

`build/buzzos.img` 是一块 raw 磁盘。当前默认布局：

| 区域 | 用途 |
| --- | --- |
| LBA 0 | boot sector |
| LBA 1..767 | kernel 预留区，最多 383.5 KiB |
| LBA 768..1279 | `/fs` minifs 区域，256 KiB |

这些数字来自 [Makefile](../Makefile)：

```makefile
KERNEL_SECTORS := 767
FS_START_SECTOR := 768
FS_SECTORS := 512
```

也来自 [src/kernel/fs/minifs/minifs.h](../src/kernel/fs/minifs/minifs.h)：

```c
MINIFS_LBA_START = 768,
MINIFS_SECTORS = 512,
```

关键点：

- `/` 是 initrd/ramfs，重启后回到编译时内置状态。
- `/dev` 是 devfs，不落盘。
- `/fs` 是 minifs，写入 `build/buzzos.img` 的固定扇区。
- 默认 `make` 会保留旧镜像里的 `/fs` 区域。
- `make image-reset-fs` 会重置 `/fs`。

因此你可以这样验证持久化：

```text
mkdir /fs/persist
write /fs/persist/msg hello-after-reboot
cat /fs/persist/msg
reboot
```

如果 `reboot` 在你的环境里直接退出 QEMU，这是正常的。重新运行：

```powershell
make run-current
```

然后在 shell 里：

```text
cat /fs/persist/msg
```

能看到原内容，就说明 `/fs` 已经落盘。

---

## 7. 文件系统实验

这一章只用 shell，不需要改代码。

### 7.1 查看目录

```text
ls /
ls /dev
ls /fs
```

目录项末尾带 `/` 表示目录。

### 7.2 创建目录和文件

```text
mkdir /fs/a
touch /fs/a/t
ls /fs/a
stat /fs/a/t
```

`touch` 走的是用户态 `create()`，最终进入内核 VFS 的 create 路径。

### 7.3 写文件、读文件

```text
write /fs/a/t hello
cat /fs/a/t
stat /fs/a/t
```

`write` 命令会用：

```c
open(path, O_CREAT | O_TRUNC | O_WRONLY)
write(fd, text, len)
close(fd)
```

所以重复 `write` 同一个文件会覆盖旧内容。

### 7.4 改名

```text
mv /fs/a/t /fs/a/u
ls /fs/a
cat /fs/a/u
```

`mv` 当前对应内核的 `rename`，主要用于同一个文件系统内改名或移动。

### 7.5 删除文件和目录

```text
rm /fs/a/u
rmdir /fs/a
ls /fs
```

POSIX 风格里：

- `rm`/`unlink` 删除普通文件。
- `rmdir` 删除空目录。
- 非空目录不能直接 `rmdir`。

### 7.6 相对路径

```text
cd /fs
mkdir rel
write rel/msg relative-path
cat rel/msg
pwd
cd /
cat /fs/rel/msg
```

相对路径由 VFS 根据当前 task 的 cwd 解析。

---

## 8. 进程、线程和后台任务实验

BuzzOS 当前有用户进程，也有同进程内的用户线程。

内置 demo 程序是 `/hello`，源码在 [src/user/bin/hello.c](../src/user/bin/hello.c)。

### 8.1 前台运行

```text
exec /hello
```

你会看到主线程和子线程交替输出：

```text
=== BuzzOS Multithreading Demo ===
Spawned thread tid=...
[main]   tick 0
[thread] tick 0
...
Thread joined. Sleeping 10s...
Done!
[exec] exited 0
```

输出可能出现轻微交错，这是多线程共享 console 的结果。用户态 `printf` 已经尽量一次 `write` 输出一段 buffer，但两个 task 并发写同一设备时，仍可能在调度点交替。

### 8.2 后台运行

两种写法都支持：

```text
exec /hello &
```

或者：

```text
exec /hello bg
```

后台运行时，shell 不等待程序结束，会直接回到提示符。

### 8.3 查看进程

```text
ps
```

常见输出：

```text
PID  STATE    OUT    CODE  NAME
0    READY    tty    0     idle
1    RUNNING  tty    0     shell
4    SLEEP    null   0     hello
```

字段含义：

| 字段 | 意义 |
| --- | --- |
| `PID` | task id |
| `STATE` | `READY/RUNNING/SLEEP/DEAD` 等 |
| `OUT` | 输出目标，前台通常是 `tty`，后台常是 `null` |
| `CODE` | 退出码 |
| `NAME` | task 名称 |

默认 `ps` 不显示已经清理掉的 dead task。要看 dead task：

```text
ps -a
```

### 8.4 等待后台进程

等待任意子进程：

```text
wait
```

等待指定 PID：

```text
wait 4
```

### 8.5 杀进程

```text
kill 4
```

`kill` 当前是教学版接口：按 task id 标记任务退出，不是完整 Unix signal 语义。

---

## 9. 网络实验

网络依赖 QEMU 的 NE2000 设备。请用 `make run` 或 Makefile 里的等价 QEMU 参数启动。

### 9.1 DHCP

BuzzOS 启动时内核会初始化网络。你也可以在 shell 里发 DHCP discover：

```text
dhcp
```

预期类似：

```text
dhcp offer 10.0.2.15
```

### 9.2 ping

QEMU user-mode network 的宿主侧网关通常是 `10.0.2.2`：

```text
ping 10.0.2.2
```

也可以测试 DNS：

```text
ping example.com
```

如果 DNS 失败，先看 QEMU 是否用 `-netdev user` 启动，再看宿主网络是否可用。

### 9.3 wget

```text
wget example.com
wget 10.0.2.2 8080
tcptwotest 10.0.2.2 8081 8082
```

这个命令在 shell 里叫 `wget`，实现函数名仍是 `cmd_sockget`。它会：

1. 解析 IPv4 字面量，或 `dns_resolve(host, &ip)`
2. `socket(AF_INET, SOCK_STREAM, 0)`
3. `connect(... port 80 或指定端口 ...)`
4. `send()` HTTP/1.0 GET 请求
5. 循环 `recv()` 并写到 console

建议先用 `example.com`。一些大型站点会重定向、分块、压缩或关闭 HTTP 明文入口，这些都超出当前小网络栈的舒适区。
在 QEMU user network 下，`10.0.2.2` 指向宿主机；smoke 测试会在宿主机启动一个本地 HTTP 服务，再用 `wget 10.0.2.2 <port>` 验证 TCP socket 路径。
`tcptwotest` 会打开两个 TCP stream socket，先发两个请求，再先读第二个、后读第一个，用来验证 per-socket PCB demux 和接收缓冲不会互相覆盖。

如果看到部分 HTML 输出后提示 recv 结束或失败，要结合实际日志判断。当前 TCP client 是第一版实现，已经能完成基本 HTTP/1.0 拉取，但还不是成熟 TCP 协议栈。

---

## 10. IPC 和同步实验

### 10.1 pipe

```text
pipetest
```

预期：

```text
pipe: hello through pipe
```

这个测试会：

1. `pipe(fds)` 得到读端和写端。
2. 往写端 `write()` 一段字符串。
3. 从读端 `read()` 出来。
4. 打印结果。

内核实现位于 [src/kernel/fs/pipefs.c](../src/kernel/fs/pipefs.c)。它通过 VFS 的 open file/fd 表把 pipe 也做成 fd 类型，这和 Unix 的思路一致：普通文件、设备、pipe 都能通过 fd 读写。

### 10.2 futex

```text
futextest
```

预期：

```text
futex: woke
```

测试逻辑：

1. 主线程把一个用户态整数设为 0。
2. `spawn()` 创建子线程。
3. 主线程在这个整数仍为 0 时调用 `futex_wait(&word, 0)`。
4. 子线程 sleep 一小段时间，把整数改成 1。
5. 子线程调用 `futex_wake(&word, 1)`。
6. 主线程醒来并 `join()` 子线程。

这已经具备 futex 的核心形状：用户态先看内存值，只有需要等待时才进内核。内核会把等待 task 标成 `BLOCKED`，`futex_wake` 再按地址唤醒匹配 waiter；`futexblocktest` 和 `/proc/sync` 可以观察这个状态。

---

## 11. 启动链：BIOS 到 C 内核

现在回头看底层。BuzzOS 的启动链从 [src/boot/boot.asm](../src/boot/boot.asm) 开始。

### 11.1 BIOS 做了什么

x86 BIOS 启动时会把启动盘第一个扇区读到物理地址：

```text
0x7C00
```

然后跳过去执行。这个扇区必须以 `0x55AA` 结尾，大小正好 512 字节。

### 11.2 boot sector 做了什么

`boot.asm` 主要做这些事：

1. 保存 BIOS 传入的启动盘号 `DL`。
2. 用 BIOS `int 15h/e820` 读取内存布局，放到低地址缓冲区。
3. 用 BIOS `int 13h/42h` LBA 扩展读，把 kernel 从 LBA 1 开始读到高一点的内存。
4. 打开 A20。
5. 加载 GDT。
6. 设置 `CR0.PE` 进入保护模式。
7. 切到 32 位代码段。
8. 把 kernel 复制到最终地址 `0x100000`。
9. 跳到内核入口。

当前常量：

```asm
%define KERNEL_FINAL_ADDR 0x100000
%define KERNEL_HIGH_SEG   0x1000
%define KERNEL_SECTORS    767
%define READ_CHUNK        64
```

这里的细节很重要：

- boot sector 自己在 `0x7C00`。
- kernel 先读到 `0x10000` 附近，避免覆盖 boot sector。
- 进入保护模式后再复制到最终地址 `0x100000`，避开 `0xA0000` VGA/BIOS hole。
- `READ_CHUNK=64` 是为了避开 BIOS 单次读取扇区数限制。

### 11.3 为什么 kernel 预留区是 767 扇区

一个扇区 512 字节：

```text
767 * 512 = 392704 bytes = 383.5 KiB
```

Makefile 和 boot.asm 必须一致。如果内核超过 boot.asm 加载的扇区数，`tools/mkimage.ps1` 会直接报错，例如：

```text
Kernel is ... bytes, but boot.asm loads only ... bytes.
```

这类错误说明构建脚本和 bootloader 的容量配置不一致，不能靠忽略解决。要么减小内核，要么同步增加 boot.asm 和 Makefile 的 kernel sectors。

---

## 12. 内核初始化顺序

内核入口在 [src/kernel/core/kernel.c](../src/kernel/core/kernel.c) 的 `_start()`。

当前顺序是：

```text
zero_bss
serial_init
gdt_install
idt_install
enable x87 FPU
syscall_init
pmm_init
paging_init
keyboard_init
vfs_init
vfs_mkdir("/bin")
ramfs_register("/hello")
ramfs_register("/bin/sh")
net_init
vga_init
sched_init
timer_init
exec_start("/bin/sh")
task_yield loop
```

为什么这个顺序不能随便换：

- `.bss` 必须先清零，否则全局变量初值不可靠。
- 串口尽早初始化，后面出错才有日志。
- GDT/IDT 要在进入复杂内核逻辑前装好，异常才能被捕获。
- syscall table 要在用户态程序运行前注册。
- PMM/paging 要早于用户地址空间和 task 创建。
- VFS 要早于 `ramfs_register`、`exec_start`。
- `timer_init` 放在 `sched_init` 后面，因为时钟中断会触发调度。
- 最后通过 `exec_start` 启动用户态 `/bin/sh`，内核主线程只负责 yield。

这也是调试内核启动问题的主线：如果日志停在某一步，就重点看这一步和它前后的依赖。

---

## 13. 中断、异常和基础驱动

### 13.1 GDT 和 TSS

GDT/TSS 相关代码在 [src/kernel/arch/i386/gdt.c](../src/kernel/arch/i386/gdt.c)。

BuzzOS 至少需要：

- kernel code segment
- kernel data segment
- user code segment
- user data segment
- TSS

TSS 的关键作用是从用户态进入内核态时提供 kernel stack。没有正确 TSS，用户态 syscall 或中断返回很容易变成 `#GP`、`#PF` 或直接三重故障。

### 13.2 IDT、异常和中断入口

IDT 代码在 [src/kernel/arch/i386/idt.c](../src/kernel/arch/i386/idt.c)，汇编入口在 [src/kernel/arch/i386/isr.asm](../src/kernel/arch/i386/isr.asm)。

常见异常：

| 向量 | 名称 | 常见原因 |
| --- | --- | --- |
| `0x00` | `#DE` | 除零，也可能是错误跳转导致执行垃圾指令后的连锁问题 |
| `0x01` | `#DB` | 调试异常，常和 EFLAGS.TF 或错误 iret 状态有关 |
| `0x06` | `#UD` | 执行了非法指令，常见于 EIP 跳到 0 或坏地址 |
| `0x0D` | `#GP` | 段权限、iret frame、用户/内核态切换错误 |
| `0x0E` | `#PF` | 页错误，访问未映射或权限错误地址 |

异常输出里的几个寄存器最有用：

```text
EIP=...
CS=...
ESP=...
SS=...
EAX=...
Error=...
```

判断方法：

- `CS=0x08` 通常说明在内核态。
- `CS=0x1B` 通常说明在用户态。
- `EIP=0x00000000` 或很小的值，多半是函数返回地址坏了。
- `ESP=0` 通常是 task 切换或栈构造坏了。
- `#PF Error` bit 可以判断读写、用户态、present 等原因。

### 13.3 串口、VGA、键盘、timer

相关文件：

- [src/kernel/drv/serial.c](../src/kernel/drv/serial.c)
- [src/kernel/drv/vga.c](../src/kernel/drv/vga.c)
- [src/kernel/drv/keyboard.c](../src/kernel/drv/keyboard.c)
- [src/kernel/drv/timer.c](../src/kernel/drv/timer.c)

串口是最重要的调试输出，因为即使 VGA 或 shell 坏了，`-serial stdio` 仍可能看到日志。

timer 是抢占调度的来源。多任务相关 bug 经常表现为：

- 前台程序能跑，后台程序卡住。
- `sleep` 不醒。
- `wait` 等不到。
- 两个任务同时输出后 shell 不回提示符。

这时要同时看 `task.c`、`timer.c`、`syscall.c`。

---

## 14. 内存管理和用户态地址空间

### 14.1 PMM

PMM 在 [src/kernel/mm/pmm.c](../src/kernel/mm/pmm.c)。它根据 bootloader 收集的 E820 memory map 建立物理页 bitmap。

你可以把 PMM 理解成：

```text
给我一个空闲 4 KiB 物理页
释放这个物理页
```

后面的 paging、用户地址空间、内核数据结构都会用它。

### 14.2 paging

分页代码在 [src/kernel/arch/i386/paging.c](../src/kernel/arch/i386/paging.c)。

BuzzOS 需要分页来做这些事：

- 内核映射。
- 用户程序 text/data/bss 映射。
- 用户栈映射。
- task 切换时切换地址空间。

用户程序当前链接在 `0x200000` 附近。用户栈也在用户地址范围内。syscall 层用范围检查保护内核，不直接信任用户传入指针。

### 14.3 用户指针校验

syscall 入口在 [src/kernel/syscall/syscall.c](../src/kernel/syscall/syscall.c) 里检查用户指针：

- 字符串必须在用户地址范围内，并且有限长度内遇到 `\0`。
- buffer 必须完整落在用户地址范围内。

这一步非常必要。否则用户程序传一个坏指针，比如 `0xFFFFFFFF`，内核直接解引用就会被用户态打崩。

当前校验是范围型，后续可以升级成页表级校验：逐页确认 present、user、write 权限。

---

## 15. ELF 用户程序、libc 和 syscall ABI

### 15.1 用户程序怎么编译

用户程序源码分成 libc 和具体命令：

- [src/user/libc/crt0.c](../src/user/libc/crt0.c)：用户态入口。
- [src/user/libc/libc.c](../src/user/libc/libc.c) / [src/user/libc/libc.h](../src/user/libc/libc.h)：小 libc 和 syscall wrapper。
- [src/user/bin/shell.c](../src/user/bin/shell.c)：shell。
- [src/user/bin/hello.c](../src/user/bin/hello.c)：demo。

Makefile 会把用户程序链接到：

```text
0x200000
```

链接脚本由 Makefile 生成：

```ld
ENTRY(_start)
SECTIONS {
  . = 0x200000;
  .text : { *(.text.entry) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
```

然后 `tools/mkinitrd.py` 把用户程序变成内核里的 initrd 数据。

### 15.2 用户程序怎么启动

启动 shell 的路径：

```text
kernel.c
  -> exec_start(initrd_bin_sh_data, INITRD_BIN_SH_SIZE, "sh", 0)
  -> elf loader
  -> task_create
  -> user_enter
  -> user crt0 _start
  -> main(argc, argv)
```

`exec /hello` 的路径：

```text
shell
  -> spawn_process_args("/hello", argv, argc, flags)
  -> syscall
  -> vfs_open("/hello")
  -> read ELF into kernel buffer
  -> exec_start_args(...)
  -> scheduler runs new task
```

### 15.3 syscall ABI

用户态 wrapper 在 [src/user/libc/libc.c](../src/user/libc/libc.c)。syscall 号也在那里定义。

大体规则：

- `EAX` 放 syscall number。
- `EBX/ECX/EDX/ESI/EDI` 放参数。
- 返回值从 `EAX` 回来。

例如用户态：

```c
int fd = open("/fs/a/t", O_CREAT | O_WRONLY);
write(fd, "hello", 5);
close(fd);
```

内核侧会走：

```text
sys_open_console_aware
sys_write
sys_close
```

### 15.4 文件 API

用户态常用文件 API 在 [src/user/libc/libc.h](../src/user/libc/libc.h)：

```c
int open(const char *path, int flags);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);
int close(int fd);
int stat(const char *path, struct stat *st);
int getdents(int fd, struct dirent *ents, size_t count);
int lseek(int fd, int offset, int whence);
int mkdir(const char *path);
int rmdir(const char *path);
int unlink(const char *path);
int rename(const char *old_path, const char *new_path);
```

flags：

```c
O_RDONLY
O_WRONLY
O_RDWR
O_CREAT
O_TRUNC
O_APPEND
```

这已经足够写一些小型 POSIX 风格用户程序。

---

## 16. VFS、mount table 和 minifs

### 16.1 为什么需要 VFS

如果没有 VFS，每个 syscall 都要判断：

```text
这是 /dev 吗？
这是 /fs 吗？
这是 initrd 文件吗？
这是 pipe 吗？
```

这样会很快变乱。

VFS 的目标是把“路径解析、fd 管理、mount 查找”放在公共层，让具体文件系统只实现自己的操作。

现在的代码已经按这个边界拆开：

| 文件 | 负责内容 |
| --- | --- |
| [src/kernel/fs/vfs.c](../src/kernel/fs/vfs.c) | 路径规范化、mount 查找、fd/open file 生命周期、通用 read/write/close/stat 分发 |
| [src/kernel/fs/vfs_internal.h](../src/kernel/fs/vfs_internal.h) | VFS 内部共享结构，例如 `fs_ops`、`open_file`、fd 表辅助函数 |
| [src/kernel/fs/ramfs.c](../src/kernel/fs/ramfs.c) | `/` 上的内存文件树和 initrd 文件 |
| [src/kernel/fs/devfs.c](../src/kernel/fs/devfs.c) | `/dev` 设备表，`console`、`serial`、`null` |
| [src/kernel/fs/minifs_vfs.c](../src/kernel/fs/minifs_vfs.c) | 把 minifs 的磁盘实现包装成 VFS mount |
| [src/kernel/fs/pipefs.c](../src/kernel/fs/pipefs.c) | pipe 读写端点和 fd 生命周期 |

当前挂载点：

| mount | 类型 | 作用 |
| --- | --- | --- |
| `/` | ramfs/initrd | 内置 `/hello`、`/bin/sh` |
| `/dev` | devfs | `console`、`serial`、`null` |
| `/fs` | minifs | 持久化文件系统 |

如果以后要加 FAT、ext2、tmpfs 或 procfs，正常做法不是继续往 `vfs.c` 塞 `if` 分支，而是新增一个模块实现 `struct fs_ops`，初始化时调用 `vfs_mount("/mountpoint", &ops)`。

### 16.2 fd 是什么

fd 是进程看到的整数句柄：

```text
0 stdin
1 stdout
2 stderr
3 ...
```

fd 背后可以是：

- 普通文件。
- 目录。
- 字符设备。
- pipe 端点。
- socket 另走 socket table，但用户态看起来也是整数句柄风格。

后台任务的 stdout 可能被设置到 `null`，这样后台程序不会一直抢 shell 输出。

### 16.3 minifs 的设计

minifs 在 [src/kernel/fs/minifs/minifs.c](../src/kernel/fs/minifs/minifs.c)。

它的目标不是完整 ext2，而是一个“像 ext 的最小落盘文件系统”：

- superblock。
- inode table。
- block bitmap。
- directory entry。
- direct blocks 和一级 indirect block。
- 固定大小。
- 无日志。

关键常量：

```c
MINIFS_BLOCK_SIZE  512
MINIFS_INODES      128
MINIFS_BLOCKS      382
MINIFS_DIRECT      8
MINIFS_INDIRECT_ENTRIES 256
MINIFS_NAME_LEN    24
```

这意味着：

- 文件名最长约 23 字符。
- inode 数量有限。
- 单文件大小受 direct + 一级 indirect block 数限制，当前理论上限约 132 KiB。
- 不适合大文件，但适合教学和 shell 实验。

### 16.4 块缓存

块缓存位于 [src/kernel/block/cache.c](../src/kernel/block/cache.c)。

当前策略是简单 write-through：

- 读扇区时先查 cache，miss 再 ATA 读盘。
- 写扇区时更新 cache，同时写回 ATA。

这样做的好处：

- minifs 不需要每次都直接打 ATA。
- 后续做 FAT/ext2 时可以复用 block 层。
- write-through 比 write-back 简单，不需要复杂 flush 策略。

---

## 17. 网络栈和用户态 socket

### 17.1 网络初始化

内核启动时调用 `net_init()`。相关文件：

- [src/kernel/net/net.c](../src/kernel/net/net.c)
- [src/kernel/net/netdev.c](../src/kernel/net/netdev.c)
- [src/kernel/drv/ne2000.c](../src/kernel/drv/ne2000.c)

QEMU 参数给系统提供 NE2000 ISA 网卡：

```text
-device ne2k_isa,netdev=n0,iobase=0x300,irq=10
```

如果改了 iobase 或 irq，驱动也要对应调整。

### 17.2 当前支持的协议

| 层 | 当前能力 |
| --- | --- |
| Ethernet | NE2000 收发帧 |
| ARP | IP 到 MAC 解析 |
| IPv4 | 基础封包/解析 |
| ICMP | echo request/reply，用于 `ping` |
| UDP | datagram，用于 DHCP |
| DNS | 查询 A 记录 |
| TCP | client 连接，用于 HTTP GET |

### 17.3 socket API

用户态声明在 [src/user/libc/libc.h](../src/user/libc/libc.h)：

```c
int socket(int domain, int type, int protocol);
int bind(int sd, const struct sockaddr_in *addr, size_t addrlen);
int connect(int sd, const struct sockaddr_in *addr, size_t addrlen);
int send(int sd, const void *buf, size_t len, int flags);
int recv(int sd, void *buf, size_t len, int flags);
int sendto(int sd, const void *buf, size_t len, int flags,
           const struct sockaddr_in *addr, size_t addrlen);
int recvfrom(int sd, void *buf, size_t len, int flags,
             struct sockaddr_in *addr, size_t addrlen);
int closesocket(int sd);
int dns_resolve(const char *host, uint32_t *ip_out);
int net_info(uint8_t mac[6], uint32_t *ip_out);
```

`ping` 使用 raw ICMP socket。`wget` 使用 TCP stream socket。`dhcp` 使用 UDP datagram socket。

### 17.4 网络排查顺序

网络不通时按这个顺序查：

1. QEMU 是否带了 `-netdev user` 和 `ne2k_isa`。
2. 是否能看到 `[ne2000] rx`、`[ne2000] tx ok`。
3. ARP 是否收到 reply。
4. DNS 是否能 resolve。
5. TCP 是否完成 SYN/SYN-ACK/ACK。
6. HTTP 站点是否支持明文 80 端口。

先测：

```text
ping 10.0.2.2
```

再测：

```text
ping example.com
```

最后测：

```text
wget example.com
wget 10.0.2.2 8080
```

---

## 18. 图形桌面、framebuffer 和 GUI app

BuzzOS 的 GUI 不是内核里的窗口系统，而是一个普通用户程序：

```text
/bin/gui
```

这个设计很重要。它意味着内核只负责提供最小硬件抽象：

- 进入或退出 VGA 图形模式。
- 把一块用户态 framebuffer 拷贝到显存。
- 提供 PS/2 鼠标状态。
- 保留原来的 `/dev/console` 键盘输入路径。

窗口、按钮、Paint、GUI Shell、APP MANAGER、鼠标光标形状，都在 [src/user/bin/gui.c](../src/user/bin/gui.c) 里完成。这比一开始就在内核里写完整桌面要稳得多，也更适合教学：你可以清楚地看到“内核机制”和“用户态界面”的边界。

### 18.1 启动 GUI

在 BuzzOS shell 里输入：

```text
gui
```

shell 会启动 `/bin/gui`，等待它退出，然后回到文本 shell。

进入后默认看到 APP MANAGER，而不是直接打开某个 app。当前内置入口：

- `PAINT`：简单绘画程序。
- `SHELL`：GUI 内置 shell 面板。
- `REFRESH`：重新扫描 `/fs/apps`。
- `/fs/apps/*`：用户放进去的外部 app。

基本操作：

- 鼠标移动光标。
- 左键点击 app、按钮或画布。
- 方向键也能移动光标，作为没有鼠标时的 fallback。
- `Esc` 或 `Ctrl+C`：在 app 内返回管理器；在管理器内退出 GUI。

这套交互路径适合当前阶段：先有一个能用的图形环境，再逐步演进成 GUI server。

### 18.2 当前 GUI 的分层

可以把它分成四层：

| 层 | 代码位置 | 责任 |
| --- | --- | --- |
| VGA 驱动 | [src/kernel/drv/vga.c](../src/kernel/drv/vga.c) | 文本模式、13h 图形模式、像素/矩形/文字/blit |
| 鼠标驱动 | [src/kernel/drv/mouse.c](../src/kernel/drv/mouse.c) | 初始化 PS/2 mouse、解析三字节数据包、维护坐标和按钮 |
| 图形 syscall | [src/kernel/syscall/sys_gfx.c](../src/kernel/syscall/sys_gfx.c) | 把用户态 `gfx_*`/`mouse_get` 调用转给 VGA 和 mouse 驱动 |
| GUI 用户程序 | [src/user/bin/gui.c](../src/user/bin/gui.c) | 管理器、Paint、GUI Shell、外部 app 启动、光标绘制 |

核心原则是：内核不保存窗口树，不知道按钮，不知道 Paint，也不负责 app 管理。内核只提供“能画”和“能读输入”的最小能力。

### 18.3 图形 syscall

用户态 libc 暴露了这些函数，声明在 [src/user/libc/libc.h](../src/user/libc/libc.h)：

```c
int gfx_mode(int mode);
int gfx_clear(int color);
int gfx_putpixel(int x, int y, int color);
int gfx_fill_rect(int x, int y, int w, int h, int color);
int gfx_text(int x, int y, const char *s, int fg, int bg);
int fb_blit(int x, int y, int w, int h, const uint8_t *pixels);
int mouse_get(struct mouse_state *out);
```

对应 syscall 号在 [src/kernel/syscall/syscall.h](../src/kernel/syscall/syscall.h)：

| syscall | 编号 | 用途 |
| --- | ---: | --- |
| `SYS_GFX_MODE` | 43 | `0` 回文本模式，`1` 进入 VGA 13h |
| `SYS_GFX_CLEAR` | 44 | 清空 320x200 图形屏幕 |
| `SYS_GFX_PUTPIXEL` | 45 | 写一个像素 |
| `SYS_GFX_FILL_RECT` | 46 | 填充矩形 |
| `SYS_GFX_TEXT` | 47 | 用内置 5x7 字体画字符串 |
| `SYS_FB_BLIT` | 48 | 从用户态 framebuffer 批量拷贝到显存 |
| `SYS_MOUSE_GET` | 49 | 获取鼠标坐标、按钮和序列号 |

注意：当前图形模式是 VGA mode 13h：

```text
320 x 200 x 256 colors
```

它是真正的线性 framebuffer，但不是 VBE/Bochs 那种高分辨率 framebuffer。显存基址在 VGA 侧是 `0xA0000`，用户程序不能直接写这个物理地址，而是通过 syscall 或 `fb_blit()` 间接更新。

### 18.4 为什么 GUI 用 `fb_blit`

如果每画一个像素都调用一次 syscall，GUI 会非常慢：

```text
用户态 -> int 0x80 -> 内核 -> 返回用户态
```

这个路径适合偶尔画点，不适合每帧画几万像素。所以 `/bin/gui` 的做法是：

1. 用户态维护一个 `fb[320 * 200]`。
2. 所有控件、文字、画布、鼠标光标都先画到这个数组。
3. 每帧只调用一次：

```c
fb_blit(0, 0, 320, 200, fb);
```

这就是当前 BuzzOS 的 framebuffer 模型。它的好处是：

- 内核接口很小。
- 用户态绘制逻辑自由。
- 每帧 syscall 数量低。
- 后面换成更高分辨率 framebuffer 时，用户态 GUI 结构不用完全推倒。

### 18.5 鼠标输入路径

PS/2 键盘和鼠标共享 8042 控制器，所以鼠标驱动不能随便读端口 `0x60`。当前设计是：

1. `mouse_init()` 初始化辅助 PS/2 设备。
2. IDT 注册 IRQ12。
3. IRQ12 只在状态位显示“这是 mouse 数据”时读取 `0x60`。
4. `mouse_handler()` 解析三字节 PS/2 packet。
5. 内核保存：

```c
struct mouse_state {
    int x;
    int y;
    int buttons;
    int dx;
    int dy;
    uint32_t seq;
};
```

`seq` 是事件序号。GUI 每帧调用 `mouse_get()`，如果 `seq` 变了，说明鼠标有新数据，再更新光标位置。这样用户态不需要知道 PS/2 packet 细节。

键盘仍然走 `/dev/console`：

```text
IRQ1 -> keyboard_handler -> keyboard buffer -> /dev/console read()
```

这也是为什么 GUI 里方向键、`Esc`、`Ctrl+C` 可以继续工作：它复用了系统已有的 console 输入。

### 18.6 APP MANAGER 和 `/fs/apps`

`/bin/gui` 启动后先扫描：

```text
/fs/apps
```

扫描方式是普通文件系统 API：

```c
open("/fs/apps", O_RDONLY);
getdents(fd, ents, sizeof(ents));
```

所以 GUI app 的管理规则非常简单：

- 内置 app：写在 `src/user/bin/gui.c` 里，例如 Paint 和 GUI Shell。
- 外部 app：放在 `/fs/apps/<name>`。
- 管理器点击外部 app 后，用 `spawn_process_args()` 启动它。
- 外部 app 退出后，GUI manager 回到图形模式并重新显示管理器。

当前 `/fs/apps` 只是一个启动约定，还不是完整桌面协议。外部 app 如果想自己画图，需要自己调用 `gfx_mode(1)`、`gfx_clear()`、`gfx_text()`、`fb_blit()` 等接口。以后如果做 GUI server，外部 app 就不应该直接控制全屏图形模式，而是通过 server 请求窗口和事件。

### 18.7 Paint 和 GUI Shell 怎么实现

Paint 的核心状态很小：

- 一块画布数组。
- 当前颜色。
- 鼠标按下时记录上一点。
- 鼠标拖动时画线。

绘画不直接写显存，而是写用户态 `canvas[]`，每帧 render 时再把 canvas 画进 `fb[]`。

GUI Shell 不是完整 `/bin/sh`，而是 GUI 内的轻量命令面板。它支持：

```text
help
ls [path]
cat <path>
echo <text>
apps
run <path>
clear
```

它用普通 VFS syscall 读目录和文件，用 `spawn_process_args()` 跑外部程序。这个设计能展示一个关键点：GUI 只是用户态程序，它仍然可以像普通 shell 一样使用文件系统和进程接口。

### 18.8 在 BuzzOS 内写一个最小 GUI app

下面这个例子用 `nano` + `basm` 在 BuzzOS 里写一个外部图形程序。它会进入图形模式、清屏、显示文字、停留 2 秒，然后回到文本模式退出。

先准备目录：

```text
mkdir /fs/apps
```

编辑源码：

```text
nano /fs/apps/card.asm
```

写入：

```asm
bits 32
global _start

section .text
_start:
    mov eax, 43        ; gfx_mode(1)
    mov ebx, 1
    int 0x80

    mov eax, 44        ; gfx_clear(11)
    mov ebx, 11
    int 0x80

    mov eax, 46        ; gfx_fill_rect(54, 62, 212, 58, 1)
    mov ebx, 54
    mov ecx, 62
    mov edx, 212
    mov esi, 58
    mov edi, 1
    int 0x80

    mov eax, 47        ; gfx_text(86, 86, msg, 15, 1)
    mov ebx, 86
    mov ecx, 86
    mov edx, msg
    mov esi, 15
    mov edi, 1
    int 0x80

    mov eax, 9         ; sleep_ms(2000)
    mov ebx, 2000
    int 0x80

    mov eax, 43        ; gfx_mode(0)
    xor ebx, ebx
    int 0x80

    mov eax, 1         ; exit(0)
    xor ebx, ebx
    int 0x80

section .data
msg db "HELLO GUI APP", 0
```

汇编：

```text
basm /fs/apps/card.asm /fs/apps/card
```

运行方式有两个：

```text
/fs/apps/card
```

或者进入 GUI：

```text
gui
```

在 APP MANAGER 里点 `REFRESH`，然后点击 `card`。

这个例子说明了当前外部 GUI app 的本质：它仍然是一个普通 ELF32 用户程序，只是调用了图形 syscall。它没有窗口、没有焦点协议，也不会和 manager 同屏并发显示。

### 18.9 下一步应如何演进 GUI

当前实现适合教学和快速实验，但如果要做真正桌面，建议按这个顺序走：

1. 把键盘和鼠标统一成内核 input event queue。
2. 把 `/bin/gui` 拆成 `draw`、`input`、`manager`、`paint`、`shell` 几个文件。
3. 做 `/dev/fb0` 或更标准的 framebuffer 对象。
4. 让 GUI manager 升级为 GUI server。
5. 外部 app 不再直接 `gfx_mode(1)`，而是向 GUI server 请求窗口。
6. GUI server 负责合成窗口、派发事件、管理焦点。

这条路线比一开始直接写复杂窗口系统更稳。BuzzOS 现在已经有了最小可用图形路径，下一步是把协议和边界变清楚。

---

## 19. 常见问题排查

### 19.1 `build/buzzos.img` 或 `build/user/*.o` 被占用

现象：

```text
Permission denied
```

或者链接/写镜像失败。

常见原因：QEMU 还在运行，正在占用镜像或旧文件。

处理：

1. 关闭 QEMU。
2. 确认没有残留 `qemu-system-i386.exe`。
3. 重新运行 `make`。

### 19.2 kernel 太大

现象：

```text
Kernel is ... bytes, but boot.asm loads only ... bytes.
```

原因：bootloader 读取的 kernel 扇区数小于实际 kernel 大小。

当前应该是 767 扇区，也就是 383.5 KiB。要检查：

- [Makefile](../Makefile) 的 `KERNEL_SECTORS`。
- [src/boot/boot.asm](../src/boot/boot.asm) 的 `KERNEL_SECTORS`。
- [tools/mkimage.ps1](../tools/mkimage.ps1) 的参数传递。

这三个地方必须一致。

### 19.3 `ls` 卡住

常见原因：

- `getdents` 没有正确推进目录 offset。
- 目录项损坏。
- VFS path 解析进入错误 mount。
- minifs 锁没有释放。

排查：

1. 先试 `ls /`、`ls /dev`、`ls /fs`，确认是哪一个挂载点卡住。
2. 如果只有 `/fs` 卡住，怀疑 minifs。
3. 如果所有目录都卡住，怀疑 VFS fd/getdents 公共层。
4. 可以用 `make image-reset-fs` 排除旧磁盘镜像数据损坏。

### 19.4 `exec /hello` 后 `#UD`

`#UD` 是 invalid opcode。常见原因不是用户程序真的有非法指令，而是 EIP 跳坏了。

重点看：

- 异常时 `CS` 是用户态还是内核态。
- `EIP` 是否为 `0x00000000`、`0x00000003` 这类明显坏地址。
- 用户线程函数返回时是否有 trampoline。
- task stack 构造是否写入了正确返回地址。
- `user_enter` 的 entry 和 stack 是否正确。

如果是线程函数执行完后崩溃，通常要看 `spawn()` 和 `sys_spawn()` 的返回 trampoline。

### 19.5 `#GP` 出现在程序退出后

常见原因：

- iret frame 构造错。
- 用户态返回内核态时段寄存器不对。
- task 已经退出但 scheduler 又切回去了。
- 内核栈或用户栈被覆盖。

排查：

1. 看 `EIP/CS/ESP/SS`。
2. 如果 `CS=0x08`，重点查内核路径。
3. 如果 `CS=0x1B`，重点查用户返回路径。
4. 看 task 状态是否已经 DEAD 还被调度。

### 19.6 后台任务不回 shell

后台任务应该：

- `exec /hello &` 立即回 shell。
- 后台 stdout 通常是 `null`。
- `ps` 能看到任务。

如果不回 shell，查：

- shell 是否在 `waitpid`。
- `spawn_process_args` flags 是否传了 background。
- `exec_start_args` 是否设置 silent/stdio。
- scheduler 是否还在调度 shell。

### 19.7 `/fs` 数据不见了

先确认你是不是执行了：

```powershell
make image-reset-fs
```

它会清空 `/fs`。

普通 `make` 默认保留旧镜像的 `/fs` 区域。如果你删除了整个 `build` 目录，旧镜像当然也没了。

### 19.8 `wget` 没输出或失败

先用简单目标：

```text
wget example.com
wget 10.0.2.2 8080
```

再看日志：

- DNS 有没有 resolved。
- ARP 有没有 reply。
- TCP 有没有 connected。
- recv 是否收到数据。

大型网站可能使用 HTTPS、跳转、压缩、分块传输或复杂 TCP 行为。BuzzOS 当前适合测试简单 HTTP/1.0。

### 19.9 backspace 显示异常

shell 的输入处理在 [src/user/bin/shell.c](../src/user/bin/shell.c)。它接受：

```text
\b
0x7F
```

并输出：

```text
\b ' ' \b
```

如果某个终端发送其他控制序列，需要扩展 `read_line()`。

### 19.10 GUI 退出后文本模式异常

GUI 正常退出路径应该是：

```text
/bin/gui -> gfx_mode(1) -> render loop -> gfx_mode(0) -> exit
```

如果退出后文字变形、花屏或 shell 看起来还在但显示不正常，按这个顺序查：

1. [src/user/bin/gui.c](../src/user/bin/gui.c) 退出前是否一定调用 `gfx_mode(0)`。
2. [src/kernel/drv/vga.c](../src/kernel/drv/vga.c) 的 `vga_set_mode(0)` 是否恢复 80x25 text registers。
3. 字体 plane 是否恢复到文本访问模式。
4. `vga_clear()` 是否在回文本模式后执行，而不是仍处在 13h 图形模式。
5. shell 是否只是显示错，还是 `/dev/console` 已经读不到键盘。

显示问题和输入问题要分开看。显示异常通常在 VGA mode restore；完全不能输入通常在键盘 IRQ 或 PS/2 控制器。

### 19.11 GUI 鼠标不动或不能点击

鼠标路径是：

```text
PS/2 mouse -> IRQ12 -> mouse_handler -> mouse_state -> SYS_MOUSE_GET -> /bin/gui
```

排查顺序：

1. QEMU 是否真的把鼠标事件交给虚拟机窗口。
2. IDT 是否注册 IRQ12，PIC 是否 unmask IRQ2 cascade 和 IRQ12。
3. IRQ12 stub 读 `0x64` 状态位时是否确认 bit 5 表示 auxiliary device。
4. `mouse_init()` 是否保留键盘 IRQ1 和 scancode set 1 translation，不要为了启用 mouse 打断键盘。
5. `mouse_get()` 返回的 `seq` 是否变化。
6. GUI 是否把 `mouse_state.x/y` 同步到 `pointer_x/pointer_y`。

不要在用户态或普通轮询代码里直接读 `0x60`。PS/2 键盘和鼠标共享输出缓冲，错误读取会把键盘 scancode 或鼠标 packet 抢走。

---

## 20. 继续扩展 BuzzOS

下面是比较合理的路线，按优先级排列。

### 20.1 继续加固 futex/同步语义

当前 futex 已经接入 scheduler 阻塞队列，后续可以继续补更接近 POSIX/Linux 的边界：

- 更明确的错误码和中断/取消语义。
- 多 waiter 唤醒顺序和公平性。
- 更细的等待原因诊断。
- 更多 kill/timeout/wake 交错回归测试。

相关文件：

- [src/kernel/syscall/syscall.c](../src/kernel/syscall/syscall.c)
- [src/kernel/sched/task.c](../src/kernel/sched/task.c)

### 20.2 完善 TCP 多连接能力

当前 TCP client 已经把连接状态拆到每个 socket 的 PCB，并具备轻量输入 demux 和 per-PCB 接收缓冲。要更可靠地支持多个并发 TCP 连接，还需要：

- 独立 seq/ack/window/retransmit 状态。
- 更完整的乱序/重复包处理。
- 更大的 recv buffer 和流控。
- close state。
- 超时和重传。

相关文件：

- [src/kernel/net/net.c](../src/kernel/net/net.c)
- [src/kernel/syscall/syscall.c](../src/kernel/syscall/syscall.c)

### 20.3 增加 `fork` / `execve`

现在有 `spawn_process_args()`，但还不是 Unix 的 `fork + execve` 模型。

要做 `fork`：

- 复制当前地址空间。
- 复制 fd table 引用。
- 父进程返回 child pid。
- 子进程返回 0。

要做 `execve`：

- 保留 pid。
- 替换用户地址空间。
- 构造 argv/envp。
- 重新进入新 entry。

### 20.4 shell 管道和重定向

有了 `pipe()`、`dup()`、`dup2()` 后，就可以做：

```text
echo hello | cat
/bin/echo hello | /bin/cat
echo hello | cat | cat
echo saved > /fs/out
echo again >> /fs/out
cat < /fs/out
```

当前 shell 支持多段 `|` 连接外部程序，并支持 `<`、`>`、`>>` 基础重定向。内部通过 `pipe()`、`dup2()` 和 `SPAWN_FLAG_INHERIT_STDIO` 只把整理好的 fd 0/1/2 交给子进程，避免临时 pipe 端泄漏。pipe 读空/写满时已经会阻塞并由对端唤醒；下一步可以继续补 shell 引号、转义、环境变量展开和更完整的 job control。

### 20.5 minifs fsck 和更完整语义

minifs 可以继续补：

- fsck：检查 inode、block bitmap、目录项一致性。
- 更多 direct/indirect block。
- 更完整 rename 语义。
- 文件系统版本号。
- 更明确的错误码。

### 20.6 procfs

可以挂一个 `/proc`：

```text
/proc/tasks
/proc/threads
/proc/meminfo
/proc/fds
/proc/net
/proc/sync
/proc/mounts
```

这样 shell 不需要专门 syscall 也能读取系统状态，更接近 Unix 风格。

### 20.7 GUI server 和事件队列

当前 GUI 是全屏用户态 manager。要继续向真正桌面演进，可以拆成两步：

第一步，做统一 input event queue：

- 键盘事件和鼠标事件进入同一个内核队列。
- 用户态通过 `read()` 或 syscall 取 `EVENT_KEY`、`EVENT_MOUSE_MOVE`、`EVENT_MOUSE_BUTTON`。
- GUI 不再同时轮询 `/dev/console` 和 `mouse_get()`。

第二步，把 `/bin/gui` 变成 GUI server：

- server 独占 framebuffer。
- 外部 app 不直接 `gfx_mode(1)`。
- app 通过 IPC 或专用 syscall 请求创建窗口、绘制内容、接收事件。
- server 管理焦点、窗口层级、关闭按钮和重绘。

这样可以保留现在已有的 Paint、GUI Shell 和 `/fs/apps` 概念，同时把“全屏 app launcher”升级成“多 app 图形系统”。

---

## 最后：推荐学习路线

如果你是按教程学习 BuzzOS，建议按这个顺序动手：

1. `make run` 跑起来。
2. 在 shell 里完成文件系统、后台进程、网络、pipe/futex 实验。
3. 读 [src/user/bin/shell.c](../src/user/bin/shell.c)，理解用户态如何调用 libc。
4. 读 [src/user/libc/libc.c](../src/user/libc/libc.c)，理解 syscall ABI。
5. 读 [src/kernel/syscall/syscall.c](../src/kernel/syscall/syscall.c) 和同目录下的 `sys_file.c`、`sys_proc.c`、`sys_net.c`、`sys_ipc.c`，把用户 API 对到内核实现。
6. 读 [src/kernel/fs/vfs.c](../src/kernel/fs/vfs.c)，理解 fd 和 mount。
7. 读 [src/kernel/sched/task.c](../src/kernel/sched/task.c)，理解调度和进程/线程。
8. 读 [src/kernel/drv/vga.c](../src/kernel/drv/vga.c)、[src/kernel/drv/mouse.c](../src/kernel/drv/mouse.c) 和 [src/user/bin/gui.c](../src/user/bin/gui.c)，理解 GUI 为什么能保持在用户态。
9. 最后读 [src/boot/boot.asm](../src/boot/boot.asm) 和 [src/kernel/core/kernel.c](../src/kernel/core/kernel.c)，把启动链补完整。

这条路线比一开始就啃 bootloader 更顺，因为你会先知道这个系统最终跑起来是什么样，再回头理解每一层为什么存在。
