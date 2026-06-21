# BuzzOS 极简操作系统手把手教程（极致版）

读法建议：
- 第一次看：按顺序从 §0 走到 §14，把仓库当成一个“能跑的小型实验”走一遍。
- 之后回查：直接用目录定位到对应章节，命令和验证步骤是独立的。

> 教程和代码是同步的。教程里贴出的代码片段都直接来自仓库源文件；只要源文件没改，按教程步骤一定能复现。

---

## 目录

- [0. 仓库目标与启动链速览](#0-仓库目标与启动链速览)
- [1. 准备开发环境](#1-准备开发环境)
- [2. 仓库文件地图与角色](#2-仓库文件地图与角色)
- [3. 启动链整体原理（BIOS → C 内核）](#3-启动链整体原理bios--c-内核)
- [4. BIOS 把控制权交给启动扇区](#4-bios-把控制权交给启动扇区)
- [5. 启动扇区把后续内核读进内存](#5-启动扇区把后续内核读进内存)
- [6. 进入 32 位保护模式](#6-进入-32-位保护模式)
- [7. 跳到 C 语言内核入口](#7-跳到-c-语言内核入口)
- [8. VGA 文本显存写屏](#8-vga-文本显存写屏)
- [9. 清零 `.bss` 的意义与做法](#9-清零-bss-的意义与做法)
- [10. 构建管线全景（Makefile 每一行在做什么）](#10-构建管线全景makefile-每一行在做什么)
- [11. 亲手做三个改动并验证](#11-亲手做三个改动并验证)
- [12. 出错了怎么排查](#12-出错了怎么排查)
- [13. 下一步要补的能力（按推荐顺序）](#13-下一步要补的能力按推荐顺序)
- [14. 怎么“读”这份仓库](#14-怎么读这份仓库)
---

## 0. 仓库目标与启动链速览

BuzzOS 是一个**教学型极简 OS 骨架**。它故意只做最小闭环：

```text
BIOS -> boot sector -> protected mode -> C kernel -> VGA text output
```

四件事，没有多余的：

1. x86 机器上电后，BIOS 自检并把磁盘第 0 个扇区读到 `0x7C00`。
2. 启动扇区在 16 位实模式里用 BIOS 磁盘中断，把后续内核扇区读到 `0x1000`。
3. 启动扇区打开 A20、加载 GDT、置位 CR0.PE，跳到 32 位保护模式。
4. 汇编 `jmp` 到 C 内核入口 `_start`；C 代码清 `.bss`、清屏、往 `0xB8000` VGA 显存里写字。

> **为什么先做最小闭环？** OS 是分层系统，每加一层（新能力）都需要底层先稳。
> 没有 BIOS → boot → C 这一条可靠链路，后面的内存管理、进程、ELF、VFS、图形、syscall 都没有“地基”可言。

---

## 1. 准备开发环境

本节的目标是：**在动手编译之前，让 `make` 一定可以跑通**。

### 1.1 操作系统

- Windows 10 / 11（仓库构建脚本走 PowerShell）。
- Linux / macOS 也能跑，但要改 `tools/mkimage.ps1`（见 §10.5）。

### 1.2 必需工具

| 工具          | 作用                                              | 备注                                                                                |
| ------------- | ------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `nasm`        | 把 `boot.asm` 汇编成 512 字节的纯二进制           | 必须用 NASM 风格（不是 GNU `as`）                                                   |
| `clang`       | 编译 freestanding C 内核                          | 用 `--target=i386-none-elf`，见 Makefile                                            |
| `ld.lld`      | 链接 `.o` 成 ELF                                  | 也可以用 `ld`（GNU），但 Makefile 默认 `ld.lld`                                     |
| `llvm-objcopy`| 把 ELF 转成纯二进制（剥掉 ELF 头）                | GNU `objcopy` 也可                                                                  |
| `make`        | 跑 Makefile                                       | Windows 装 MSYS2 / Git Bash / WSL 都行；构建脚本用 PowerShell 调用                   |
| `powershell`  | 调 `mkimage.ps1` 拼接镜像、清空 build 目录         | Windows 自带                                                                        |
| `qemu-system-i386` | 启动镜像看效果（可选但强烈推荐）              | 没有 QEMU 也能构建，但你看不到“屏幕输出”这一步验证                                  |

#### 1.2.1 Windows 上的安装（最省事路径）

PowerShell 装 Chocolatey，然后用 `choco` 一把梭：

```powershell
choco install -y nasm llvm make qemu
```

装完**新开一个 PowerShell 窗口**，让 PATH 生效，再验证：

```powershell
nasm -v
clang --version
ld.lld --version
llvm-objcopy --version
make --version
qemu-system-i386 --version
```

> 只要其中一项 `command not found`，回到 `choco install` 把那一项补上，不要猜能不能跑。

#### 1.2.2 验证工具链真的能交叉编译

在一个临时目录里写个 `t.c`：

```c
__attribute((noreturn)) void _start(void) { __asm__("hlt"); }
```

然后跑：

```powershell
clang --target=i386-none-elf -ffreestanding -fno-builtin -mno-sse -mno-mmx -c t.c -o t.o
ld.lld -m elf_i386 -nostdlib -Ttext=0x1000 -o t.elf t.o
```

如果没报 `linker not found` / `assembler not found`，工具链就绪。
可以删掉 `t.c t.o t.elf` 了——它们跟仓库无关。

### 1.3 QEMU 验证

```powershell
qemu-system-i386 --version
```

如果你只想要 headless 测试，先只跑 `make` 看构建是否通过；启动验证可以等 §11 再做。

---

## 2. 仓库文件地图与角色

整个仓库只有 4 个真正干活的文件，**先把这 4 个文件认清楚**，再读后面的章节就轻松。

| 文件                            | 角色         | 关键产物               | 你改它会影响什么                                          |
| ------------------------------- | ------------ | ---------------------- | --------------------------------------------------------- |
| [`src/boot/boot.asm`](../src/boot/boot.asm)            | 启动扇区     | `build/.../boot.bin`（512 字节） | BIOS 看到的启动消息、加载内核扇区数、保护模式切换         |
| [`src/kernel/kernel.c`](../src/kernel/kernel.c)        | C 内核入口   | `build/.../kernel.o`                | 屏幕输出内容、颜色、清屏逻辑                              |
| [`linker.ld`](../linker.ld)                            | 链接脚本     | 决定内核布局在 `0x1000`              | 装载地址、入口符号、`.bss` 起止、`.text.entry` 顺序        |
| [`tools/mkimage.ps1`](../tools/mkimage.ps1)            | 镜像打包     | `build/buzzos.img`                   | 启动扇区 + 内核 + 填充扇区的拼接方式                       |

辅助文件：

- [`Makefile`](../Makefile) —— 上面 4 个文件的调用顺序、参数。
- [`README.md`](../README.md) / [`README.en.md`](../README.en.md) —— 项目说明。
- [`docs/README.md`](README.md) —— 本目录的入口。
- `build/` —— 一切中间产物都在这里，构建后自动生成，**不要手动改**。

> 读仓库的顺序建议：`linker.ld` → `boot.asm` → `kernel.c` → `mkimage.ps1` → `Makefile`。
> 因为链接脚本先决定了“内核要放哪”，启动扇区才知道往哪读，C 内核才知道自己会被放在 `0x1000`。

---

## 3. 启动链整体原理（BIOS → C 内核）

整条链路，**时间轴**视角：

```text
   ┌────────────────────────────────────────────────────────────────┐
t= │ 阶段              │ CPU 模式 │ 关键动作                        │
───┼───────────────────┼──────────┼─────────────────────────────────┤
1  │ BIOS 自检         │ 16 位    │ POST，找可启动设备              │
2  │ 加载启动扇区      │ 16 位    │ 把扇区 0 读到 0x7C00            │
3  │ 校验 0x55AA       │ 16 位    │ 启动签名验证                    │
4  │ 跳转 0x7C00       │ 16 位    │ 跳到 boot sector                │
5  │ 读后续内核扇区    │ 16 位    │ int 13h/AH=42h 读到 0x1000      │
6  │ 打开 A20          │ 16 位    │ 写端口 0x92                     │
7  │ 加载 GDT          │ 16 位    │ lgdt [gdt_descriptor]           │
8  │ 置位 CR0.PE       │ 16 位    │ mov cr0, eax                    │
9  │ 远跳刷流水线      │ 32 位    │ jmp CODE_SEG:protected_start    │
10 │ 切段寄存器+设栈   │ 32 位    │ mov ds/es/fs/gs/ss, ax          │
11 │ jmp 0x1000        │ 32 位    │ 进入 C 内核 _start              │
12 │ 清 .bss、清屏     │ 32 位    │ zero_bss(); clear();            │
13 │ 写 VGA 显存       │ 32 位    │ puts(...);                      │
14 │ hlt 死循环        │ 32 位    │ __asm__ volatile("hlt");        │
   └────────────────────────────────────────────────────────────────┘
```

> 关键点：**第 8→9 步是分水岭**。第 8 步还是 16 位指令；第 9 步“远跳转”是让 CPU 重新拉取 CS:IP，并按新的段描述符解读 `CODE_SEG`，从而正式进入 32 位模式。
> 远跳转的二进制 `EA` 指令被处理器内部识别为“模式切换边界”，这是硬件特性，**不能换成近跳转**。

---

## 4. BIOS 把控制权交给启动扇区

### 4.1 目标

让机器从磁盘最前面的 512 字节开始执行。

### 4.2 原理

BIOS 做完 POST 后会枚举启动设备。**对传统 BIOS 来说**，它把“第一个可启动磁盘的第 0 个扇区（512 字节）”读进物理地址 `0x7C00`，然后检查这个扇区的**最后两个字节**是不是魔术数字：偏移 510 必须是 `0x55`、偏移 511 必须是 `0xAA`。匹配就把 `CS:IP` 设到 `0x0000:0x7C00` 跳过去。

> 这就是为什么“启动扇区”这个名字很形象——**它就是磁盘上第 0 扇区**，由 BIOS 直接喂给 CPU。

### 4.3 你要看哪几行

[`src/boot/boot.asm`](../src/boot/boot.asm) 的关键片段：
bits 16
org 0x7c00          ; 告诉 NASM：标签地址从 0x7C00 开始算
                    ; 这决定了 jmp/call/mov 等用到的“相对偏移”怎么算
                    ; CPU 真的从 0x7C00 取指令，是 BIOS 决定的（见 §4.2）

start:
    cli             ; 关闭中断。我们马上要改段寄存器，先别被打断
    xor ax, ax
    mov ds, ax      ; DS = 0
    mov es, ax      ; ES = 0
    mov ss, ax      ; SS = 0
    mov sp, 0x7c00  ; 栈顶 0x7C00（往低地址长），跟启动扇区在同一个区域
    sti             ; 重新开中断
```

**为什么要重置 `DS/ES/SS`？** BIOS 跳转过来时这些寄存器的值是 BIOS 留下的、不可预测。把它们清零是为了让后面的 `int 13h`、`lgdt`、段寄存器装填都用我们能掌控的值。

**为什么栈顶设在 `0x7C00`？** 启动扇区只占 512 字节。把栈顶设在 `0x7C00`、往低地址长，**栈不会覆盖到启动扇区自身的代码**——这是实模式下能用的最干净位置。

### 4.4 启动签名

文件最后两行：

```asm
times 510 - ($ - $$) db 0
dw 0xaa55
```

- `times 510 - ($ - $$) db 0`：从当前位置填 0 到偏移 `510`。
- `dw 0xaa55`：在偏移 `510` 写入 `0x55`，偏移 `511` 写入 `0xAA`（小端序）。

如果 BIOS 读出来这 512 字节的最后两个字节不是 `0x55 0xAA`，机器会拒绝启动，**直接黑屏或打一行“Operating system not found”**。

> **为什么是 510 而不是 511？** 因为 `dw 0xaa55` 占 2 字节（偏移 510、511），`times` 表达式要在 `dw` **前面**算长度，所以填到 510。

### 4.5 打印第一行

```asm
mov [boot_drive], dl    ; BIOS 把启动盘号放在 DL 里
mov si, loading_msg     ; SI -> 字符串
call print
```

`print` 是个小循环：用 `lodsb` 取字符，再用 BIOS `int 10h / AH=0x0E`（TTY 输出）写屏。`BX=0x0007` 是颜色（黑底白字）。

### 4.6 验证

```powershell
make
qemu-system-i386 -drive format=raw,file=build\buzzos.img
```

你应该看到屏幕上打印 `BuzzOS boot` 这一行（在切到保护模式之前；进入 C 内核后清屏会把它擦掉，所以这一行只能在 BIOS/启动扇区阶段看到）。

如果想保留这一行看效果，把 `kernel.c` 里的 `clear();` 注释掉，再 `make` 一次。

### 4.7 常见坑

| 现象                         | 原因                                                            |
| ---------------------------- | --------------------------------------------------------------- |
| 黑屏，什么都不显示           | `0x55AA` 签名丢失、扇区大小不是 512、BIOS 找不到启动盘         |
| 直接 `Operating system...`   | BIOS 跳过启动扇区（签名错、磁盘格式错、UEFI 启动了）            |
| 显示乱码                     | 段寄存器没清零，`int 10h` 用错段                                |

---

## 5. 启动扇区把后续内核读进内存

### 5.1 目标

把磁盘上 512 字节之后的内核读到内存固定地址 `0x1000`，为下一步跳过去做准备。

### 5.2 原理

启动扇区只占 512 字节，**装不下完整内核**。所以启动扇区变成一个“迷你加载器”：用 BIOS 磁盘扩展读（EDD / int 13h AH=42h）把后续扇区读进内存。

> **为什么不用 `AH=02h`（旧式 CHS 读）？** `AH=42h` 走 LBA，**不依赖磁盘几何参数**（柱面/磁头/扇区），对 QEMU、U 盘、真实硬盘行为一致，更适合跨环境。

### 5.3 磁盘地址包（DAP）

代码里这一段：

```asm
align 4
dap:
    db 0x10        ; DAP 长度 16 字节
    db 0           ; 保留
    dw KERNEL_SECTORS    ; 要读几个扇区
    dw KERNEL_LOAD_ADDR  ; 目标偏移（段内偏移，段=0）
    dw 0x0000            ; 目标段 = 0
    dq 1                ; 起始 LBA = 1（即跳过第 0 扇区，从第 1 扇区开始读）
```

`KERNEL_LOAD_ADDR = 0x1000`、`KERNEL_SECTORS = 64`，对应 Makefile 里 `KERNEL_SECTORS := 64`。

- 目标内存地址 = `0x0000:0x1000` = 物理 `0x1000`。
- 起始 LBA = 1 表示“从磁盘第 1 扇区开始读”，刚好跳过第 0 扇区（启动扇区自己）。
- 64 扇区 × 512 字节 = 32768 字节（32 KiB）的内核容量上限。

### 5.4 调用

```asm
mov ah, 0x42
mov dl, [boot_drive]
mov si, dap
int 0x13
jc disk_error
```

- `AH = 0x42`：扩展读。
- `DL = boot_drive`：从 BIOS 拿到的启动盘号，必须保存后再用，**BIOS 跳过来时 DL 才是有效的，跳过一次就丢了**。
- `SI = dap`：DAP 的地址。
- `int 0x13`：触发读盘。
- `jc disk_error`：CF=1 表示读失败。

### 5.5 失败处理

```asm
disk_error:
    mov si, disk_error_msg
    call print
.halt:
    hlt
    jmp .halt
```

> **`jc` 的判断不可省略**。`int 13h AH=42h` 在 QEMU 里几乎不会失败，但**真机、U 盘、CD-ROM 上读盘失败是常见事件**。如果不 `jc`，启动扇区会拿着 0x1000 里的“残留内存”当内核跳进去——结果是跳到乱码、CPU 异常三连。

### 5.6 你要约束的地址

要保证 3 件事一致：

1. 启动扇区在 `0x7C00`，512 字节。
2. 内核加载到 `0x1000`，且**不与启动扇区冲突**。
3. 链接器把内核入口放在 `0x1000`（`linker.ld` 里 `. = 0x1000;`）。

> **`0x1000` 是什么地位？** 在实模式 1 MiB 内存布局里，`0x00000-0x003FF` 是中断向量表 + BIOS 数据区；`0x00400-0x004FF` 是 BIOS 工作区；`0x00500-0x07BFF` 是 DOS 习惯用法；`0x07C00-0x07DFF` 是启动扇区。所以从 `0x1000`（4 KiB）开始放内核是安全的。

### 5.7 验证

把 `KERNEL_SECTORS` 改大一点（比如 `66`），看 `mkimage.ps1` 校验是否报警：

```
Kernel is X bytes, but boot.asm loads only 32768 bytes.
```

不匹配就会触发这条保护。再改回去或同步修改 Makefile 与 `boot.asm` 的常量。

---

## 6. 进入 32 位保护模式

### 6.1 目标

从 16 位实模式切到 32 位保护模式，**这之后 CPU 用的就是 32 位指令和段描述符**。

### 6.2 原理（为什么需要切换）

实模式是 8086 时代的遗产：20 位地址、段寄存器左移 4 位 + 偏移、单任务、没有保护。
要做哪怕一点点像样的系统（用平坦内存模型、做分页、装用户态），都必须先切到保护模式。

保护模式的核心变化：

- 段寄存器变成**段选择子**（selector），指向 GDT/LDT 中的**段描述符**。
- 寻址变成 `选择子:偏移`，段基址、段限、属性全部由描述符定义。
- 32 位偏移，最大段内寻址 4 GiB。
- 后面才能开分页（CR0.PG）。

### 6.3 打开 A20

```asm
in  al, 0x92
or  al, 00000010b
out 0x92, al
```

为什么要做这一步？

> 8086 只有 20 位地址线（A0-A19），**1 MiB 之内**寻址。
> 80286 开始有 24 位地址线，但为了跟老 8086 软件兼容，故意把 A20 线“封死”，造成 1 MiB 处的回卷（`0xFFFFF+1=0x00000`）。
> 进入保护模式、地址会接近 `0x100000`（比如栈顶设在 `0x90000`、栈可能再往上长），**A20 必须打开**。
> 端口 `0x92` 的 bit1 是“系统端口 A20 gate”，写 1 即打开。这是古老但通用且简单的做法，**比键盘控制器方式（端口 `0x60/0x64`）简单**。

### 6.4 加载 GDT

```asm
align 8
gdt_start:
    dq 0                ; 索引 0：空描述符，必须存在，CPU 故意让它代表“无效选择子”

gdt_code:
    dw 0xffff           ; 段限 0xFFFFF（粒度 4 KiB → 4 GiB）
    dw 0x0000           ; 基址低 16 位 = 0
    db 0x00             ; 基址中 8 位 = 0
    db 10011010b        ; P=1, DPL=0, S=1, Type=0xA (代码段, 可读, 一致位=0)
    db 11001111b        ; G=1, D/B=1, L=0, AVL=0, 段限高 4 位 = 0xF
    db 0x00             ; 基址高 8 位 = 0

gdt_data:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b        ; P=1, DPL=0, S=1, Type=0x2 (数据段, 向上扩展, 可写)
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start        ; 注意：低 32 位，BIOS 还在 16 位实模式
```

- `dq 0` = 描述符 0：必须有的“空描述符”。
- 代码/数据描述符：基址 0、段限 `0xFFFFF`、粒度 4 KiB。**加起来就是 0–4 GiB 的平坦段**，所有段寄存器都指向 0 起的同一段。
- `D/B = 1`：在代码段上代表“32 位默认操作数/地址”。

随后加载：

```asm
lgdt [gdt_descriptor]
```

### 6.5 置位 CR0.PE

```asm
mov eax, cr0
or  eax, 1
mov cr0, eax
```

`CR0` 的 bit0 是 PE（Protection Enable）。置 1 后，CPU **从下一条指令开始按保护模式解读段寄存器**。但此时流水线里还有按 16 位解码的旧指令，必须用一次远跳转把流水线冲掉：

```asm
jmp CODE_SEG:protected_start
```

`CODE_SEG = gdt_code - gdt_start` 是个 16 位选择子，编译时算好（值是 8，因为 `gdt_code` 在 GDT 第 1 项，每项 8 字节）。

### 6.6 进入 32 位后的整理

```asm
bits 32
protected_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000   ; 32 位栈顶
    jmp KERNEL_LOAD_ADDR   ; 跳到 C 内核
```

`mov ds, ax` 里的 `ax` 是 `DATA_SEG`（值是 16，gdt_data 是 GDT 第 2 项）。段选择子 **0x10** 装到 `DS/ES/FS/GS/SS`，对应平坦数据段，**32 位**。

> 栈顶 `0x90000` 是临时栈，离开 BIOS 区、离内核 `0x1000` 有 ~560 KiB 距离，**够 freestanding C 跑一会不爆栈**。以后要换更好的栈。

### 6.7 验证

正常路径下你应该看不到这一步的任何输出。如果 `qemu-system-i386` 直接重启、毫无输出，问题大概率在 GDT/CR0 这一段：

- GDT 描述符数值算错：检查 `dw/dd/dq` 顺序、大小端。
- `lgdt` 的地址对不上：标签偏移算错。
- `jmp CODE_SEG:protected_start` 没用远跳转、或者 `CODE_SEG` 算成 0。

排查手段：用 QEMU 的 `-d int` 把中断/异常打印出来，或加 `-s -S` 接 gdb。

---

## 7. 跳到 C 语言内核入口

### 7.1 目标

把控制权从汇编交给 `src/kernel/kernel.c` 里的 `void _start(void)`。

### 7.2 链接脚本（`linker.ld`）

```ld
ENTRY(_start)

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text.entry)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss*)
    }
    __bss_end = .;
}
```

逐行解释：

- `ENTRY(_start)`：链接器把 `_start` 当作入口符号写到 ELF 头。运行时不强制要求，但**调试器和启动器会用它**。
- `. = 0x1000;`：接下来所有符号的地址都从 `0x1000` 开始算。**这与启动扇区把内核读到 `0x1000` 完全一致**。
- `.text` 段：先放所有 `.text.entry`（`_start` 用这个 section 属性），再放其他 `.text*`。**保证 `_start` 在最前**。
- `.rodata` / `.data` 顺序照常。
- `__bss_start = .;` / `__bss_end = .;`：**两个导出符号**，C 代码靠它知道 `.bss` 在哪。

### 7.3 C 端的入口属性

[`src/kernel/kernel.c`](../src/kernel/kernel.c)：

```c
__attribute__((section(".text.entry"), used, noreturn))
void _start(void)
{
    zero_bss();
    clear();
    puts("BuzzOS\n");
    ...
}
```

- `section(".text.entry")`：放到 `.text.entry` 段（前面 `.text` 块先收这一段），所以 `_start` 一定在 `0x1000` 附近。
- `used`：防止编译器优化掉（它没有显式 caller）。
- `noreturn`：告诉编译器 `hlt` 之后没有路径，**别在那生成无用的清理代码**。

### 7.4 freestanding 是什么

`_start` 是 freestanding C 程序入口，**和 host C 程序不一样**：

- **没有 `main` 之前**的 libc 初始化代码。
- **没有堆**（除非你提供 `sbrk`）。
- **没有标准输入输出**。
- **没有 atexit、thread-local storage 的自动构造**。
- **栈、对齐、调用约定**还在（C 编译器保证了）。

> 这意味着：**所有基础设施要自己写**。一旦后面加 `printf`、加 `malloc`、加 `pthread`，全是自己实现。

### 7.5 验证

把 `_start` 里 `puts("BuzzOS\n");` 改成 `puts("hi\n");`，重新 `make`、用 QEMU 启动，应该看到 `hi` 而不是 `BuzzOS`。

---

## 8. VGA 文本显存写屏

### 8.1 目标

通过往 `0xB8000` 写 16 位“字符+颜色”单元，让屏幕上出现文字。

### 8.2 原理

传统 VGA 文本模式（mode 3）下，显卡把 `0xB8000` 起的 32 KiB 显存映射成“80 列 × 25 行 × 每格 2 字节”。
每 2 字节：

| 字节 | 含义                                  |
| ---- | ------------------------------------- |
| 0    | ASCII 字符                            |
| 1    | 颜色属性：高 4 位背景、低 4 位前景    |

颜色属性：

| 位  | 含义                                       |
| --- | ------------------------------------------ |
| 7   | 闪 / 亮背景（mode 3 下通常当作背景高亮）   |
| 6-4 | 背景色（0=黑、1=蓝、2=绿、3=青、4=红...） |
| 3   | 亮前景                                     |
| 2-0 | 前景色                                     |

`0x0F` = 黑底亮白，`0x4F` = 红底亮白，`0x1F` = 蓝底亮白。

### 8.3 C 端代码

```c
enum {
    VGA_WIDTH = 80,
    VGA_HEIGHT = 25,
    VGA_ADDR = 0xb8000,
};

static volatile uint16_t *const vga = (uint16_t *)VGA_ADDR;
static uint8_t row;
static uint8_t col;
static uint8_t color = 0x0f;
```

- `volatile`：编译器别把这个指针“优化”成寄存器里的常量，**必须每次都去内存读**。
- `uint16_t *`：每写一个 `uint16_t` 就是一格。

`vga_cell`：

```c
static uint16_t vga_cell(char ch)
{
    return (uint16_t)(uint8_t)ch | ((uint16_t)color << 8);
}
```

低 8 位是字符，高 8 位是颜色。

`clear`：

```c
for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
    vga[i] = vga_cell(' ');
}
row = 0;
col = 0;
```

填 2000 格空格 = 整屏清空。**注意它假设了 row/col 的当前值会被立刻重置**——一旦你改成多线程环境，这里要加锁。

`newline`：

```c
col = 0;
if (++row < VGA_HEIGHT) return;
// 否则整屏上滚一行
for (size_t y = 1; y < VGA_HEIGHT; y++)
    for (size_t x = 0; x < VGA_WIDTH; x++)
        vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];

row = VGA_HEIGHT - 1;
for (size_t x = 0; x < VGA_WIDTH; x++)
    vga[row * VGA_WIDTH + x] = vga_cell(' ');
```

- 走到最后一行再回车，**把第 1..24 行上移到 0..23，最后一行清空**。
- 注意 `row = VGA_HEIGHT - 1;` 之后清空的是新最后一行，**不能错成清第 `VGA_HEIGHT` 行**（那是 80×25 之外的显存，**也能写**，但屏幕上看不到）。

`putc` 和 `puts` 是最朴素实现：

```c
static void putc(char ch)
{
    if (ch == '\n') { newline(); return; }
    vga[row * VGA_WIDTH + col] = vga_cell(ch);
    if (++col == VGA_WIDTH) newline();
}

static void puts(const char *s)
{
    while (*s) putc(*s++);
}
```

### 8.4 你要理解的点

> **这套输出只支持 80×25 文本模式**。如果 BIOS 切到了别的模式（比如 framebuffer 模式），写 `0xB8000` 不会显示。
> 调试时**先确认屏幕上能看到“BIOS 启动扇区阶段”的 `BuzzOS boot`**，这能反推 BIOS 用的是文本模式 3。
> 后面要走图形界面时，应该改用 framebuffer（`0xFD000000` 等）+ 字体渲染，**这是另一条路**，不能直接拿 `0xB8000` 当万能写屏函数。

### 8.5 验证

修改 `color`：

```c
static uint8_t color = 0x1f;   // 蓝底亮白
```

重新 `make`，QEMU 启动，应该看到蓝底白字。

### 8.6 常见坑

| 现象               | 原因                                                          |
| ------------------ | ------------------------------------------------------------- |
| 字符乱码           | 写的是单字节 `char`，但 VGA cell 是 2 字节；写时**没强转 16 位** |
| 部分行不动         | 写越界，落到显存 32 KiB 范围外（屏幕会“吃掉”）                |
| 整屏闪             | 忘了 `volatile`，编译器以为“写了再读还是一样的”，**优化掉写** |

---

## 9. 清零 `.bss` 的意义与做法

### 9.1 目标

让所有“本应为 0”的全局/静态变量启动时是 0。

### 9.2 原理

`.bss` 段在 ELF/二进制里**只占段大小、但不占文件内容**——它假设“运行时是 0”。但**裸机没有 loader 帮你清零**，所以内核启动时要自己把 `__bss_start` 到 `__bss_end` 的字节全写 0。

`kernel.c` 里：

```c
extern uint8_t __bss_start;
extern uint8_t __bss_end;

static void zero_bss(void)
{
    for (uint8_t *p = &__bss_start; p < &__bss_end; p++) {
        *p = 0;
    }
}
```

`__bss_start / __bss_end` 是 `linker.ld` 导出的符号，**链接器在最终 ELF 里把它们解析成具体地址**。

### 9.3 不清零会怎样

| 全局变量类型           | 启动时内容                                  | 后果                                                          |
| ---------------------- | ------------------------------------------- | ------------------------------------------------------------- |
| 普通全局变量（如 `row`） | 启动扇区、内核加载之间的内存残留           | 启动显示位置错乱，**最早一行出现在屏幕中间**                  |
| 计数类状态             | 随机值                                      | 第一个 `if (++col == VGA_WIDTH)` 可能误触发 newline          |
| 锁标志                 | 随机值                                      | 死锁、竞争 bug（但目前单 CPU、单线程，不影响）                |
| 分配器状态             | 随机值                                      | **第一个 `malloc` 直接踩内存**                                |

> **`.bss` 段在裸机里就是脏区**。哪怕 ELF 文件里 `.bss` 段不占字节（因为全是 0），**它映射到的物理内存是真实存在的、可能含 BIOS/启动扇区残留**。

### 9.4 验证

故意把 `zero_bss()` 调用注释掉（**仅为了看效果，记得改回来**）：

```c
_start:
    // zero_bss();   // 故意关掉
    clear();
    puts("BuzzOS\n");
```

重新 `make` 启动，**第一行不一定从第 0 行第 0 列开始**。再打开 `zero_bss()` 就对了。

### 9.5 跟启动签名 (`0x55AA`) 的关系

启动签名 `0x55AA` 写在启动扇区偏移 510-511，**不会出现在 `0x1000` 起的内核区域**（因为内核从磁盘第 1 扇区开始读，第 0 扇区在 `0x7C00`）。所以即使你不清 `.bss`，也不会读到 `0x55AA`，**会读到的是 BIOS 数据区 / 启动扇区加载过程中的残留**。

---

## 10. 构建管线全景（Makefile 每一行在做什么）

### 10.1 总览

构建是一根 5 节的链条：

```text
src/boot/boot.asm  ── nasm ──▶  build/.../boot.bin       (512 字节，纯二进制)
src/kernel/kernel.c ── clang ──▶ build/.../kernel.o      (i386 ELF 重定位目标文件)
build/.../kernel.o   ── ld.lld ──▶ build/.../kernel.elf  (i386 ELF 可执行)
build/.../kernel.elf ── llvm-objcopy ──▶ build/.../kernel.bin (纯二进制，剥 ELF 头)
build/.../boot.bin + kernel.bin ──▶ mkimage.ps1 ──▶ build/buzzos.img
```

`make` 会按依赖关系自动算顺序。你只改源文件时，**只有受影响的那条链会重跑**。

### 10.2 工具与关键参数

Makefile 里：

```make
NASM   := nasm
CC     := clang
LD     := ld.lld
OBJCOPY:= llvm-objcopy
```

`CFLAGS`：

```make
--target=i386-none-elf      ; 目标：32 位 i386 裸机
-std=c11                    ; C11 标准
-ffreestanding              ; 没有宿主
-fno-builtin                ; 不展开 GCC/clang 内建函数（如 __builtin_memcpy）
-fno-stack-protector        ; 不插 canary（freestanding 没人处理 __stack_chk_fail）
-fno-pic                    ; 不生成位置无关代码
-mno-sse -mno-mmx           ; 不用 SSE/MMX，省字节、避免对齐要求
-O2                         ; 优化
-Wall -Wextra               ; 警告
```

`LDFLAGS`：

```make
-m elf_i386
-T linker.ld                ; 用我们的链接脚本
-nostdlib                   ; 不连 libc / libgcc
```

### 10.3 每条规则

```make
$(OBJDIR):
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJDIR)' | Out-Null"
```

**先建好 build 目录**。`OBJDIR` 里带一个 `BUILD_ID = Unix 时间戳`，每次构建得到一个独立子目录，**避免并发或半途失败时污染旧产物**。

```make
$(OBJDIR)/boot.bin: src/boot/boot.asm | $(OBJDIR)
	$(NASM) -f bin $< -o $@
```

`nasm -f bin`：直接输出原始二进制，**不带 ELF 头**——因为启动扇区必须从偏移 0 开始就是 `0xEB`（跳转指令），不能有 ELF 头。

```make
$(OBJDIR)/kernel.o: src/kernel/kernel.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
```

```make
$(OBJDIR)/kernel.elf: $(OBJDIR)/kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJDIR)/kernel.o
```

链接时**用链接脚本**（入口 `_start`、起始地址 `0x1000`、`.bss` 边界符号）。

```make
$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@
```

ELF → 纯二进制。**这是为了让启动扇区按固定偏移 LBA 读盘**，否则 ELF 头的尺寸会随版本变化，启动扇区里的“读 N 扇区”会算错。

```make
$(IMAGE): $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin tools/mkimage.ps1
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/mkimage.ps1 \
		-Boot $(OBJDIR)/boot.bin -Kernel $(OBJDIR)/kernel.bin \
		-Out $(IMAGE) -KernelSectors $(KERNEL_SECTORS)
```

调 PowerShell 拼镜像。

### 10.4 镜像打包脚本（`tools/mkimage.ps1`）

```powershell
$bootBytes   = [IO.File]::ReadAllBytes((Resolve-Path $Boot))
$kernelBytes = [IO.File]::ReadAllBytes((Resolve-Path $Kernel))
$maxKernelBytes = $KernelSectors * 512
```

把启动扇区和内核读进内存。

```powershell
if ($bootBytes.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes, got $($bootBytes.Length)."
}
```

**强制启动扇区必须正好 512 字节**。多 1 字节少 1 字节都直接报错。

```powershell
if ($kernelBytes.Length -gt $maxKernelBytes) {
    throw "Kernel is $($kernelBytes.Length) bytes, but boot.asm loads only $maxKernelBytes bytes."
}
```

**强制内核大小 ≤ 启动扇区能加载的容量**。这一关过了之后写盘：

```powershell
$fs.Write($bootBytes, 0, $bootBytes.Length)        ; 写 512 字节
$fs.Write($kernelBytes, 0, $kernelBytes.Length)    ; 写真实内核
$padding = $maxKernelBytes - $kernelBytes.Length
if ($padding -gt 0) {
    $zeroes = [byte[]]::new($padding)
    $fs.Write($zeroes, 0, $zeroes.Length)          ; 补 0 到扇区边界
}
```

补 0 很重要：**内核在磁盘上是“扇区对齐”的**，但 ELF 二进制末尾一般不是 512 倍数。不补 0，启动扇区读 `KERNEL_SECTORS` 个扇区时会把镜像文件末尾的随机字节也读进内存。

### 10.5 在非 Windows 上跑

`tools/mkimage.ps1` 依赖 PowerShell，Linux/macOS 上要换成 shell 或 Python 实现。等价 shell 版：

```sh
# 假设 boot.bin=512 字节，kernel.bin ≤ 32 KiB
KERNEL_SECTORS=64
# 实际路径类似 build/obj/1700000000000/boot.bin；下面用 shell 通配 + 取最新
BOOT=$(ls -1t build/obj/*/boot.bin | head -1)
KERNEL=$(ls -1t build/obj/*/kernel.bin | head -1)
OUT=build/buzzos.img
dd if=/dev/zero of="$OUT" bs=512 count=$((1 + KERNEL_SECTORS))
dd if="$BOOT"   of="$OUT" bs=512 count=1 conv=notrunc
dd if="$KERNEL" of="$OUT" bs=512 seek=1 conv=notrunc
```

然后修改 Makefile 把 `powershell -File tools/mkimage.ps1` 替换成这一段。

### 10.6 验证

逐个文件检查：

```powershell
# 取最新的 build 子目录（避免 * 通配匹配多个）
$latest = Get-ChildItem build\obj | Sort-Object Name -Descending | Select-Object -First 1
$objDir = Join-Path build\obj $latest.Name

# 启动扇区必须正好 512 字节
(Get-Item (Join-Path $objDir 'boot.bin')).Length    # → 512

# 内核二进制大小（必须 ≤ 32768）
(Get-Item (Join-Path $objDir 'kernel.bin')).Length  # → 100~1000 字节

# 镜像必须正好 1 + 64 = 65 扇区 = 33280 字节
(Get-Item build\buzzos.img).Length                  # → 33280
```

启动扇区最后两字节必须是 `0x55 0xAA`：

```powershell
$bytes = [IO.File]::ReadAllBytes((Join-Path $objDir 'boot.bin'))
$bytes[510..511] | ForEach-Object { "0x{0:X2}" -f $_ }
# → 0x55 0xAA
```

---

## 11. 亲手做三个改动并验证

> **改之前先 `git status`**，确认仓库是干净的；改完之后 `git diff` 看清楚。

### 11.1 改动 A：改启动标题（验证 BIOS 阶段）

[`src/boot/boot.asm`](../src/boot/boot.asm)：

```asm
loading_msg db "BuzzOS boot", 13, 10, 0
```

改成：

```asm
loading_msg db "MyOS booting...", 13, 10, 0
```

```powershell
make
qemu-system-i386 -drive format=raw,file=build\buzzos.img
```

期望：屏幕**最顶部**出现 `MyOS booting...`（在清屏之前）。

> 如果你看不到——大概率是 `make` 没重跑 `boot.bin`。强制清理：
>
> ```powershell
> make clean
> make
> ```

### 11.2 改动 B：改内核文本（验证 C 内核）

[`src/kernel/kernel.c`](../src/kernel/kernel.c)：

```c
puts("BuzzOS\n");
puts("minimal i686 kernel\n\n");
puts("next: memory, syscalls, ELF, VFS, framebuffer\n");
```

改成：

```c
puts("MyOS\n");
puts("hello from C kernel\n\n");
puts("custom boot line\n");
```

```powershell
make
qemu-system-i386 -drive format=raw,file=build\buzzos.img
```

期望：清屏后依次看到 `MyOS`、`hello from C kernel`、空行、`custom boot line`。

> 如果只看到 BIOS 阶段那一行、看不到你写的 C 输出：
>
> 1. 保护模式切换失败。
> 2. `_start` 没在 `0x1000`。
> 3. 链接脚本没把 `.text.entry` 排在最前。
>
> 排查用 QEMU `-d int -no-reboot`：在控制台看 `#DE`（除零）、`#GP`（段错误）、`#UD`（未定义指令）等异常。

### 11.3 改动 C：改屏幕颜色（验证 VGA 属性字节）

[`src/kernel/kernel.c`](../src/kernel/kernel.c)：

```c
static uint8_t color = 0x0f;   // 黑底亮白
```

改成：

```c
static uint8_t color = 0x1f;   // 蓝底亮白
```

```powershell
make
qemu-system-i386 -drive format=raw,file=build\buzzos.img
```

期望：所有文字变成蓝底亮白。

颜色速查：

| 值    | 含义                |
| ----- | ------------------- |
| `0x07` | 黑底灰白（CGA 默认） |
| `0x0F` | 黑底亮白            |
| `0x1F` | 蓝底亮白            |
| `0x2F` | 绿底亮白            |
| `0x4F` | 红底亮白            |
| `0xCF` | 红底亮红            |
| `0xE0` | 黄底黑字            |

> `0x0F` 是默认；**调试时把它调成 `0x4F`（红底亮白）能很快分辨“这是内核输出还是 BIOS 残留”**。

### 11.4 改动 D（进阶）：把 `.bss` 故意留脏，看 `zero_bss()` 的价值

**仅作演示，演示完务必还原**。

```c
void _start(void)
{
    // zero_bss();   // 故意注释
    clear();
    puts("no bss clear\n");
}
```

```powershell
make
qemu-system-i386 -drive format=raw,file=build\buzzos.img
```

期望：`no bss clear` **可能不显示在屏幕最顶行**，因为 `row/col` 初值是脏的，`clear()` 写入位置由脏值决定。

还原：把 `// zero_bss();` 改回 `zero_bss();`。

### 11.5 改动 E（进阶）：故意让内核超 32 KiB

故意往 `kernel.c` 加个 64 KiB 的大数组：

```c
static uint8_t big[64 * 1024] = { 0 };
```

`make` 应该失败：

```
Kernel is 65536 bytes, but boot.asm loads only 32768 bytes.
```

这正是 `mkimage.ps1` 的保护——**先报警再爆雷**。

---

## 12. 出错了怎么排查

### 12.1 黑屏（QEMU 启动后什么都看不到）

按顺序排查：

1. **`0x55AA` 在不在？**
   ```powershell
   $b = [IO.File]::ReadAllBytes((Join-Path $objDir 'boot.bin'))
   $b[510..511]  # 应该是 85 170（0x55 0xAA）
   ```
   缺了就检查 `times 510 - ($ - $$) db 0` 和 `dw 0xaa55`。

2. **构建产物大小对吗？**
   ```powershell
   (Get-Item build\buzzos.img).Length   # 应为 33280
   ```

3. **QEMU 是不是把它当 U 盘/硬盘启动？** QEMU 默认 `-drive format=raw` 启动会按磁盘扫；
   但有些版本会忽略不识别的磁盘格式，加 `format=raw,file=...` 一定要带。

4. **QEMU 调试打印**：
   ```powershell
   qemu-system-i386 -drive format=raw,file=build\buzzos.img -d int -no-reboot
   ```
   找 `#DE`、`#GP`、`#UD`、`#PF` 等异常编号。

### 12.2 重启循环

- 几乎一定是**保护模式切换问题**：
  - GDT 描述符写错；
  - A20 没打开（栈顶在 `0x90000`，A20 没开时会回卷到 `0x80000`，覆盖到内核/启动扇区）；
  - 远跳不是远跳（用了 `jmp` 没用 `jmp CODE_SEG:protected_start`）。

### 12.3 内核崩溃但 BIOS 阶段正常

`_start` 跳过去了但很快死了：

- `_start` 之前的栈顶 (`esp=0x90000`) 是不是被覆盖：GDT 描述符错误导致段限太小，写入时触发 `#GP`。
- 链接脚本 `.text.entry` 是不是真的排在最前：可以用 `llvm-objdump -d (Join-Path $objDir 'kernel.elf')` 看 `Disassembly of section .text`，**第一行应该是 `_start` 相关**。

### 12.4 QEMU 里用 GDB 单步

```powershell
# 一个窗口
qemu-system-i386 -drive format=raw,file=build\buzzos.img -s -S

# 另一个窗口
gdb
(gdb) target remote :1234
(gdb) set architecture i386
(gdb) break *0x7c00
(gdb) continue
(gdb) stepi   # 一步一步
```

跟踪每条 `mov`/`jmp` 的执行。

### 12.5 `make` 自身失败

| 报错关键字               | 原因 / 解决                                            |
| ------------------------ | ------------------------------------------------------ |
| `nasm: error: ...`        | `boot.asm` 语法错；检查 `bits 16`/`bits 32` 切换      |
| `clang: error: unknown target: i386-none-elf` | LLVM 没装 i386 target；用 `llvm-mingw` 或检查 clang 版本 |
| `ld.lld: error: undefined symbol _start`     | `kernel.c` 里 `_start` 不在；检查 `__attribute__` |
| `llvm-objcopy: command not found`           | LLVM bin 不在 PATH                                      |
| `Boot sector must be exactly 512 bytes`     | `boot.asm` 长度错了；`org 0x7c00` 后**汇编地址**不等于字节数 |
| `Kernel is X bytes, but boot.asm loads only ...` | 调大 `KERNEL_SECTORS`，或减小内核              |

---

## 13. 下一步要补的能力（按推荐顺序）

> 这条顺序不是“唯一正确”，而是**对当前 200 行不到的代码体量最友好**的演进路径。
> 每一步都让前一步的投资产生价值，不浪费任何一行代码。

1. **串口（COM1）输出** —— `inb/outb` 端口 `0x3F8`，比 BIOS 阶段早，调试更顺。
2. **GDT 初始化封装** —— 把 GDT 搬进 C，给后面加 TSS/用户态做准备。
3. **IDT + 中断处理** —— `#GP`/`#PF` 不再是黑屏死机。
4. **物理内存管理** —— bitmap / 空闲链表，从 E820 拿到内存图。
5. **分页 / 虚拟内存** —— 把内核映射到高地址（`0xC0000000`），为用户态铺路。
6. **ELF 加载器** —— 解析 ELF，把用户程序搬到内存。
7. **进程 / 线程** —— 调度、PCB、上下文切换。
8. **系统调用** —— `int 0x80` 或 `sysenter`，定义 `sys_write` 等。
9. **VFS** —— 抽象文件操作，挂载 ramfs / devfs。
10. **framebuffer 图形输出** —— 多模式 VBE，字体渲染。
11. **用户态 + 加载 `gcc` 编译的 C 程序** —— 终极目标。

> 每加一步都先回到这个教程，在对应章节补一节“这是怎么工作的、为什么这么设计、踩过什么坑”。
> 这样这份仓库能长期保持“既小又清楚”。

---

## 14. 怎么“读”这份仓库

读 OS 仓库的正确姿势是看**三个坐标**：

1. **横向坐标**：文件（`boot.asm` / `kernel.c` / `linker.ld` / `mkimage.ps1` / `Makefile`）。
2. **纵向坐标**：阶段（BIOS → 启动扇区 → 保护模式 → C 入口 → 输出）。
3. **时间坐标**：每加一项新能力（内存管理、进程…），它们在这两个轴上落在哪。

随时可以问自己：

- 这个文件**只做一件事**吗？有没有偷偷做别的事？
- 这一步**能不能关掉**？关掉之后系统行为怎么变？
- 这个符号 / 常量**为什么是这个值**？换成别的会怎样？
- 哪一步是新能力的地基？**删了它后面全倒**？

如果这四点里有一条答不上来，先回到对应章节重读一遍，再动手改代码。

---

## 附录 A：BIOS 阶段寄存器初始值

| 寄存器 | 启动扇区看到时的值                |
| ------ | --------------------------------- |
| `DL`   | 启动盘号（要立刻存）              |
| `CS:IP` | `0x0000:0x7C00`                  |
| `SS:SP` | 一般是 `0x0000:0x0400`（**别用**） |
| `DS/ES` | 不可预测                          |
| 中断   | 默认开                            |
| A20    | 通常是关的（386 兼容）            |

## 附录 B：常用端口

| 端口      | 用途                | 备注                                |
| --------- | ------------------- | ----------------------------------- |
| `0x3F8`   | COM1 数据           | 后面做串口调试时用                  |
| `0x60`/`0x64` | 键盘控制器      | 旧式 A20 开关用                      |
| `0x92`    | 系统控制端口 A      | bit1 = A20 gate，bit0 = 重启       |
| `0x1F0-0x1F7` | 主 IDE 通道     | 直连磁盘时用                        |
| `0xB8000` | VGA 文本显存        | `volatile uint16_t *` 写            |

## 附录 C：常用 QEMU 参数

| 参数                              | 作用                                  |
| --------------------------------- | ------------------------------------- |
| `-drive format=raw,file=...`      | 把本地文件当原始磁盘镜像启动          |
| `-serial stdio`                   | 串口转成 stdin/stdout                 |
| `-d int`                          | 打印 CPU 异常到 stderr                |
| `-no-reboot`                      | 异常后不自动重启（便于看错误）        |
| `-s -S`                           | 启动 gdb server 在 1234 并暂停        |
| `-m 32`                           | 限制 32 MiB 内存（强制暴露内存 bug）  |
| `-monitor stdio`                  | 把 QEMU monitor 接到 stdin             |
