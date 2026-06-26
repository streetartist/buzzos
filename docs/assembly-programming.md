# BuzzOS 汇编编程教程

这篇教程专门讲在 BuzzOS 里怎么写、读、改汇编代码。它覆盖三类场景：

- 写纯汇编用户程序，通过 `int 0x80` 调用 BuzzOS 系统调用。
- 写内核汇编函数，让 C 内核调用，或从汇编回调 C 函数。
- 读懂并修改 boot sector、中断入口、syscall 入口和上下文切换代码。

BuzzOS 目标平台是 32 位 i386。仓库里的汇编使用 NASM Intel 语法，不是 GNU `as` 的 AT&T 语法。

---

## 目录

- [1. 先看 BuzzOS 里的汇编文件](#1-先看-buzzos-里的汇编文件)
- [2. NASM 基础和构建格式](#2-nasm-基础和构建格式)
- [3. i386 寄存器、栈和调用约定](#3-i386-寄存器栈和调用约定)
- [4. BuzzOS 内存布局和段选择子](#4-buzzos-内存布局和段选择子)
- [5. 用户态汇编程序入门](#5-用户态汇编程序入门)
- [6. 用户态系统调用 ABI](#6-用户态系统调用-abi)
- [7. 写第一个纯汇编用户程序](#7-写第一个纯汇编用户程序)
- [8. 纯汇编程序读取 argc 和 argv](#8-纯汇编程序读取-argc-和-argv)
- [9. 纯汇编程序做文件 IO](#9-纯汇编程序做文件-io)
- [10. 把汇编用户程序加入 initrd](#10-把汇编用户程序加入-initrd)
- [11. C 里的内联汇编](#11-c-里的内联汇编)
- [12. 内核汇编函数](#12-内核汇编函数)
- [13. Boot sector 汇编](#13-boot-sector-汇编)
- [14. 保护模式切换](#14-保护模式切换)
- [15. 中断、异常和 IRQ 入口](#15-中断异常和-irq-入口)
- [16. syscall 入口](#16-syscall-入口)
- [17. 上下文切换](#17-上下文切换)
- [18. setjmp 和 longjmp](#18-setjmp-和-longjmp)
- [19. 调试汇编代码](#19-调试汇编代码)
- [20. 常见错误清单](#20-常见错误清单)
- [21. 练习路线](#21-练习路线)
- [附录 A. BuzzOS syscall 号](#附录-a-buzzos-syscall-号)
- [附录 B. 常用选择子和地址](#附录-b-常用选择子和地址)
- [附录 C. 常用指令速查](#附录-c-常用指令速查)

---

## 1. 先看 BuzzOS 里的汇编文件

仓库里真正的 NASM 汇编文件有这些：

| 文件 | 构建格式 | 作用 |
| --- | --- | --- |
| [src/boot/boot.asm](../src/boot/boot.asm) | `nasm -f bin` | 512 字节 boot sector，从 BIOS 接管控制权，加载内核并进入保护模式 |
| [src/kernel/arch/i386/isr.asm](../src/kernel/arch/i386/isr.asm) | `nasm -f elf32` | 异常、IRQ、syscall 的汇编入口 |
| [src/kernel/arch/i386/switch.asm](../src/kernel/arch/i386/switch.asm) | `nasm -f elf32` | task 上下文切换 |
| [src/kernel/arch/i386/setjmp.asm](../src/kernel/arch/i386/setjmp.asm) | `nasm -f elf32` | 内核用 `kernel_setjmp` / `kernel_longjmp` |

还有一些 C 文件里写了内联汇编：

| 文件 | 内容 |
| --- | --- |
| [src/user/libc/libc.c](../src/user/libc/libc.c) | 用户态 `int 0x80` syscall wrapper，x87 数学函数 |
| [src/kernel/arch/i386/user.c](../src/kernel/arch/i386/user.c) | 通过 `iret` 进入 ring 3 用户态 |
| [src/kernel/arch/i386/gdt.c](../src/kernel/arch/i386/gdt.c) | `lgdt`、加载段寄存器、`ltr` |
| [src/kernel/arch/i386/idt.c](../src/kernel/arch/i386/idt.c) | `lidt`、`sti`、异常停止循环 |
| [src/kernel/arch/i386/paging.c](../src/kernel/arch/i386/paging.c) | `cr0`、`cr3`、刷新 TLB |

读这篇教程时，建议同时打开这些文件。BuzzOS 的汇编代码量不大，适合从真实代码学习。

---

## 2. NASM 基础和构建格式

BuzzOS 用 NASM 的 Intel 语法。几个最常见写法：

```asm
bits 32
section .text
global symbol_name
extern c_function

symbol_name:
    mov eax, 1
    ret
```

### 2.1 Intel 语法顺序

NASM 的操作数顺序是：

```asm
mov destination, source
```

例子：

```asm
mov eax, 123        ; eax = 123
mov [var], eax      ; memory[var] = eax
mov eax, [var]      ; eax = memory[var]
```

这和 C 内联汇编里常见的 GNU AT&T 语法不同。Clang/GCC 默认内联汇编通常写成：

```c
__asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr));
```

这里的 `int $0x80` 是 AT&T 风格。不要把 NASM 文件里的 Intel 语法直接复制进 C 内联汇编。

### 2.2 `bits 16`、`bits 32`

`bits` 告诉 NASM 当前代码按什么模式编码：

```asm
bits 16
```

用于 BIOS 启动阶段。此时 CPU 处在 real mode，默认地址和操作数大小是 16 位，可以调用 BIOS 中断。

```asm
bits 32
```

用于保护模式内核和用户程序。BuzzOS 内核、用户态、异常入口、syscall 入口都运行在 32 位模式。

### 2.3 `-f bin` 和 `-f elf32`

BuzzOS 有两种汇编输出格式。

boot sector 用 raw binary：

```powershell
nasm -f bin src/boot/boot.asm -o build/obj/.../boot.bin
```

原因是 BIOS 要求启动扇区就是 512 字节裸机器码，不能带 ELF header。

内核汇编用 ELF32 object：

```powershell
nasm -f elf32 src/kernel/arch/i386/isr.asm -o build/obj/.../isr.o-asm
```

原因是内核要和 C 编译出来的 `.o` 文件一起交给 `ld.lld` 链接。ELF32 object 里有符号表和重定位信息，C 和汇编才能互相引用。

### 2.4 `org`、`section`、`global`、`extern`

`org` 只适合 raw binary 场景，典型就是 boot sector：

```asm
bits 16
org 0x7c00
```

这表示代码会被 BIOS 放到物理地址 `0x7C00` 执行，NASM 计算标签地址时按这个基地址处理。

ELF32 object 不要随便写 `org`，用 section：

```asm
bits 32
section .text
global switch_context
extern timer_irq
```

`global` 导出符号给 C 或链接器。`extern` 声明外部符号，比如 C 函数。

---

## 3. i386 寄存器、栈和调用约定

### 3.1 通用寄存器

| 寄存器 | 常见用途 |
| --- | --- |
| `EAX` | 返回值、syscall number、临时值 |
| `EBX` | syscall 第 1 参数，C ABI 下通常要由被调用者保存 |
| `ECX` | syscall 第 2 参数、计数器、临时值 |
| `EDX` | syscall 第 3 参数、临时值 |
| `ESI` | syscall 第 4 参数、源指针，C ABI 下通常要保存 |
| `EDI` | syscall 第 5 参数、目标指针，C ABI 下通常要保存 |
| `EBP` | 栈帧基址，C ABI 下通常要保存 |
| `ESP` | 栈顶指针 |
| `EIP` | 指令指针，不能直接 `mov` 写，通常由 `call`、`ret`、`jmp`、`iret` 改变 |
| `EFLAGS` | 标志寄存器，包含 IF、ZF、CF 等 |

### 3.2 C 调用约定

BuzzOS 的 C 代码用 i386 cdecl 风格。

规则：

- 参数从右到左压栈。
- 返回值放在 `EAX`。
- 调用者清理参数栈。
- `EAX`、`ECX`、`EDX` 是 caller-saved，函数可以随便改。
- `EBX`、`ESI`、`EDI`、`EBP` 是 callee-saved，汇编函数如果改了必须恢复。

一个 C 函数：

```c
int add3(int a, int b, int c);
```

对应汇编里取参数：

```asm
global add3
add3:
    mov eax, [esp + 4]      ; a
    add eax, [esp + 8]      ; b
    add eax, [esp + 12]     ; c
    ret
```

如果用到了 `EBX`：

```asm
global add_with_ebx
add_with_ebx:
    push ebx
    mov ebx, [esp + 8]      ; push ebx 后，原来的第 1 参数变成 [esp+8]
    mov eax, [esp + 12]
    add eax, ebx
    pop ebx
    ret
```

### 3.3 栈方向

i386 栈向低地址增长。

```asm
push eax     ; esp -= 4; [esp] = eax
pop eax      ; eax = [esp]; esp += 4
```

`call func` 等价于：

```asm
push return_address
jmp func
```

`ret` 等价于：

```asm
pop eip
```

所以只要你破坏了栈上的返回地址，`ret` 就会跳到错误位置。很多 `#UD`、`#GP`、三重故障，本质都是栈坏了。

---

## 4. BuzzOS 内存布局和段选择子

### 4.1 内核布局

内核链接脚本是 [linker.ld](../linker.ld)，入口地址从 `0x100000` 开始：

```ld
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    .text : {
        *(.text.entry)
        *(.text*)
    }
    ...
}
```

boot sector 会先把内核读到 `0x10000`，进入保护模式后再复制到 `0x100000`，最后跳到 `0x100000`，避开 `0xA0000` VGA/BIOS hole。

### 4.2 用户态布局

Makefile 生成用户程序链接脚本，把用户程序放到：

```text
0x200000
```

用户地址窗口当前是：

```text
0x001C0000 .. 0x00280000
```

默认用户栈顶：

```text
0x27F000
```

用户态入口前还有一个小 trampoline：

```text
0x1FF000
```

它负责在 ring 3 里加载用户数据段，然后跳到真正 ELF entry。

### 4.3 段选择子

BuzzOS 当前 GDT 选择子：

| 名称 | 值 | 用途 |
| --- | --- | --- |
| `GDT_SEL_KCODE32` | `0x08` | 内核代码段 |
| `GDT_SEL_KDATA32` | `0x10` | 内核数据段 |
| `GDT_SEL_UCODE32` | `0x1B` | 用户代码段，RPL=3 |
| `GDT_SEL_UDATA32` | `0x23` | 用户数据段，RPL=3 |
| `GDT_SEL_TSS` | `0x28` | TSS |

异常日志里看到：

```text
CS=0x08
```

通常表示异常发生在内核态。

看到：

```text
CS=0x1B
```

通常表示异常发生在用户态。

---

## 5. 用户态汇编程序入门

BuzzOS 用户程序是 ELF32。C 用户程序通常经过：

```text
crt0.c -> main(argc, argv) -> exit(ret)
```

纯汇编程序可以不使用 `crt0.c`，直接导出 `_start`。内核加载 ELF 后会把用户栈构造成：

```text
[esp + 0] = fake return address, 当前为 0
[esp + 4] = argc
[esp + 8] = argv
```

所以纯汇编 `_start` 可以这样读取参数：

```asm
mov eax, [esp + 4]      ; argc
mov ebx, [esp + 8]      ; argv
```

用户程序不能直接调用内核 C 函数，也不能访问内核地址。它和内核交互只应该通过 syscall。

---

## 6. 用户态系统调用 ABI

BuzzOS 用户态 syscall 使用：

```asm
int 0x80
```

寄存器约定：

| 寄存器 | 含义 |
| --- | --- |
| `EAX` | syscall number |
| `EBX` | 第 1 参数 |
| `ECX` | 第 2 参数 |
| `EDX` | 第 3 参数 |
| `ESI` | 第 4 参数 |
| `EDI` | 第 5 参数 |
| `EAX` | 返回值 |

例如 `write(fd, buf, count)`：

```asm
%define SYS_WRITE 5

mov eax, SYS_WRITE
mov ebx, 1          ; fd = stdout
mov ecx, msg        ; buf
mov edx, msg_len    ; count
int 0x80
```

返回值在 `EAX`。负数表示错误。

`exit(code)`：

```asm
%define SYS_EXIT 1

mov eax, SYS_EXIT
mov ebx, 0
int 0x80
```

`exit` 不应该返回。如果返回了，说明内核或 syscall 表坏了，程序应该停住：

```asm
.hang:
    jmp .hang
```

---

## 7. 写第一个纯汇编用户程序

新建文件：

```text
src/user/bin/asmhello.asm
```

内容：

```asm
bits 32
section .text
global _start

%define SYS_EXIT  1
%define SYS_WRITE 5

_start:
    mov eax, SYS_WRITE
    mov ebx, 1              ; stdout
    mov ecx, msg
    mov edx, msg_len
    int 0x80

    mov eax, SYS_EXIT
    xor ebx, ebx            ; exit code 0
    int 0x80

.hang:
    jmp .hang

section .rodata
msg:
    db "hello from asm", 10
msg_len equ $ - msg
```

这段程序做两件事：

1. 调用 `SYS_WRITE` 往 fd 1 写字符串。
2. 调用 `SYS_EXIT` 退出。

fd 1 是 stdout。BuzzOS 会在启动用户程序时通过 VFS 设置标准输入输出。

### 7.1 在 BuzzOS 内编辑、汇编、运行

BuzzOS 内置了一个小型汇编器 `/bin/basm`。它不是完整 NASM，但支持上面这种教学常用子集：`bits/global/section/%define/equ/label`、`db/dd`、`mov/xor/int/ret/nop/push/pop/call/jmp/jcc/add/sub/cmp` 等。

在 BuzzOS shell 里可以直接这样做：

```text
nano /fs/demo.asm
```

在 `nano` 中按 `Ctrl+T` 插入最小汇编模板，按 `Ctrl+S` 保存，按 `Ctrl+C` 退出。然后汇编并运行：

```text
basm /fs/demo.asm /fs/demo
exec /fs/demo
```

如果省略输出路径，`basm /fs/demo.asm` 会默认写出 `/fs/demo`。

---

## 8. 纯汇编程序读取 argc 和 argv

下面的程序打印 `argv[0]`。

```asm
bits 32
section .text
global _start

%define SYS_EXIT  1
%define SYS_WRITE 5

_start:
    mov eax, [esp + 4]      ; argc
    cmp eax, 1
    jl .done

    mov esi, [esp + 8]      ; argv
    mov ecx, [esi]          ; argv[0]
    call strlen             ; eax = strlen(ecx)

    mov edx, eax
    mov eax, SYS_WRITE
    mov ebx, 1
    ; ecx 已经是 argv[0]
    int 0x80

    mov eax, SYS_WRITE
    mov ebx, 1
    mov ecx, newline
    mov edx, 1
    int 0x80

.done:
    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80

strlen:
    push ecx
    xor eax, eax
.loop:
    cmp byte [ecx + eax], 0
    je .end
    inc eax
    jmp .loop
.end:
    pop ecx
    ret

section .rodata
newline:
    db 10
```

注意 `strlen` 里保存了 `ECX`。这里 `ECX` 既是输入字符串指针，又要在 syscall 中当作 `buf` 参数，所以不能让 `strlen` 破坏它。

---

## 9. 纯汇编程序做文件 IO

这个例子创建或覆盖 `/fs/asm.txt`，写入一行文本，然后关闭文件。

```asm
bits 32
section .text
global _start

%define SYS_EXIT  1
%define SYS_OPEN  2
%define SYS_CLOSE 3
%define SYS_WRITE 5

%define O_WRONLY 0x0001
%define O_CREAT  0x0100
%define O_TRUNC  0x0200

_start:
    mov eax, SYS_OPEN
    mov ebx, path
    mov ecx, O_WRONLY | O_CREAT | O_TRUNC
    int 0x80
    cmp eax, 0
    jl .fail

    mov esi, eax            ; save fd

    mov eax, SYS_WRITE
    mov ebx, esi
    mov ecx, text
    mov edx, text_len
    int 0x80

    mov eax, SYS_CLOSE
    mov ebx, esi
    int 0x80

    xor ebx, ebx
    jmp .exit

.fail:
    mov ebx, 1

.exit:
    mov eax, SYS_EXIT
    int 0x80

.hang:
    jmp .hang

section .rodata
path:
    db "/fs/asm.txt", 0
text:
    db "written by asm", 10
text_len equ $ - text
```

运行后在 BuzzOS shell 里验证：

```text
cat /fs/asm.txt
```

如果返回错误，优先确认 `/fs` 是否存在、镜像是否正常、路径字符串是否以 `0` 结尾。

---

## 10. 把汇编用户程序加入 initrd

当前 Makefile 默认只打包：

```text
/hello
/bin/sh
```

要把 `asmhello.asm` 打进系统镜像，需要扩展用户程序构建规则。下面是最小思路，按项目现有风格添加即可。

### 10.1 添加目标变量

在 Makefile 的用户程序变量附近增加：

```makefile
ASMHELLO_ELF := $(BUILD)/user/asmhello.elf
USER_ELFS := $(USER_ELF) $(SHELL_ELF) $(ASMHELLO_ELF)
```

如果原来已有 `USER_ELFS := $(USER_ELF) $(SHELL_ELF)`，改成上面这种。

### 10.2 添加汇编和链接规则

```makefile
$(BUILD)/user/asmhello.o: src/user/bin/asmhello.asm | $(BUILD)/user
	$(NASM) -f elf32 src/user/bin/asmhello.asm -o $(BUILD)/user/asmhello.o

$(ASMHELLO_ELF): $(BUILD)/user/asmhello.o build/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T build/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/asmhello.o
```

纯汇编程序不需要 `crt0.o` 和 `libc.o`，因为它自己提供 `_start` 并直接 syscall。

### 10.3 修改 initrd 打包规则

把原来的：

```makefile
$(INITRD_H): $(USER_ELFS) tools/mkinitrd.py
	python tools/mkinitrd.py /hello $(USER_ELF) /bin/sh $(SHELL_ELF) > $@
```

改成：

```makefile
$(INITRD_H): $(USER_ELFS) tools/mkinitrd.py
	python tools/mkinitrd.py /hello $(USER_ELF) /bin/sh $(SHELL_ELF) \
		/asmhello $(ASMHELLO_ELF) > $@
```

然后构建运行：

```powershell
make run
```

进入 shell 后：

```text
exec /asmhello
```

预期输出：

```text
hello from asm
```

---

## 11. C 里的内联汇编

BuzzOS 的用户态 libc 里已经有 syscall wrapper：

```c
static int syscall3(int nr, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret)
                     : "a"(nr), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}
```

这段的意思：

| 片段 | 含义 |
| --- | --- |
| `"int $0x80"` | 执行软件中断 |
| `"=a"(ret)` | 输出约束，执行后把 `EAX` 写入 `ret` |
| `"a"(nr)` | 输入约束，执行前把 `nr` 放入 `EAX` |
| `"b"(a1)` | 把 `a1` 放入 `EBX` |
| `"c"(a2)` | 把 `a2` 放入 `ECX` |
| `"d"(a3)` | 把 `a3` 放入 `EDX` |
| `"memory"` | 告诉编译器这段汇编可能读写内存，不要跨它乱重排内存访问 |

### 11.1 什么时候用内联汇编

适合用内联汇编的场景：

- 执行特殊 CPU 指令：`int`、`in`、`out`、`lgdt`、`lidt`、`ltr`、`hlt`。
- 读写控制寄存器：`cr0`、`cr3`。
- 精确控制寄存器 ABI，例如 syscall wrapper。
- 用 x87 指令实现小数学函数。

不适合用内联汇编的场景：

- 普通循环和字符串处理，C 更清楚。
- 大段逻辑，应该放到 `.asm` 文件。
- 只是为了“更快”，但没有测量。

### 11.2 常见内联汇编错误

错误示例：

```c
__asm__ volatile("mov %eax, 1");
```

问题：

- GNU inline asm 不是 NASM 语法。
- `%eax` 里的 `%` 在 GCC/Clang 模板里有特殊含义，寄存器通常写 `%%eax`。

正确写法之一：

```c
__asm__ volatile("movl $1, %%eax" ::: "eax");
```

但更推荐用约束，让编译器分配寄存器：

```c
int x = 1;
__asm__ volatile("" : : "a"(x));
```

---

## 12. 内核汇编函数

内核汇编文件用 `nasm -f elf32` 编译，并和 C 内核一起链接。

### 12.1 写一个 C 可调用的汇编函数

新建：

```text
src/kernel/arch/i386/asm_demo.asm
```

内容：

```asm
bits 32
section .text
global asm_add3

; uint32_t asm_add3(uint32_t a, uint32_t b, uint32_t c);
asm_add3:
    mov eax, [esp + 4]
    add eax, [esp + 8]
    add eax, [esp + 12]
    ret
```

在 C 里声明：

```c
#include <stdint.h>

extern uint32_t asm_add3(uint32_t a, uint32_t b, uint32_t c);
```

加入 Makefile：

```makefile
KERNEL_ASMS := \
	src/kernel/arch/i386/isr.asm \
	src/kernel/arch/i386/switch.asm \
	src/kernel/arch/i386/setjmp.asm \
	src/kernel/arch/i386/asm_demo.asm
```

如果函数修改 `EBX/ESI/EDI/EBP`，一定要保存和恢复：

```asm
global asm_use_ebx
asm_use_ebx:
    push ebx
    mov ebx, [esp + 8]
    mov eax, ebx
    pop ebx
    ret
```

### 12.2 从汇编调用 C 函数

`isr.asm` 里有典型例子：

```asm
extern exception_handler

push esp
push edx
push eax
call exception_handler
add esp, 12
```

这对应 C 函数：

```c
void exception_handler(uint32_t vector, uint32_t error, const uint32_t *frame);
```

cdecl 参数从右往左压栈。也就是说：

```asm
push frame
push error
push vector
call exception_handler
add esp, 12
```

调用者负责 `add esp, 12` 清理 3 个参数。

---

## 13. Boot sector 汇编

boot sector 是 [src/boot/boot.asm](../src/boot/boot.asm)。它是最特殊的汇编文件。

### 13.1 BIOS 进入点

BIOS 会把启动盘第一个扇区读到：

```text
0x0000:0x7C00
```

也就是物理地址：

```text
0x7C00
```

然后跳过去执行。所以文件开头是：

```asm
bits 16
org 0x7c00
```

最后必须填充到 510 字节，并写入启动签名：

```asm
times 510 - ($ - $$) db 0
dw 0xaa55
```

如果超过 512 字节，NASM 会报错。如果没有 `0x55AA` 签名，BIOS 不会把它当可启动扇区。

### 13.2 初始化段和栈

启动后先做：

```asm
cli
xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00
sti
```

这让 `DS/ES/SS` 都指向 0，栈从 `0x7C00` 往下长。`cli/sti` 是为了避免改 `SS:SP` 过程中被中断打断。

### 13.3 BIOS 打印

real mode 下可以用 BIOS `int 0x10` 打印字符：

```asm
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret
```

这只能在进入保护模式前用。进入保护模式后，BIOS 中断不能直接用了。

### 13.4 读取内核

BuzzOS 用 BIOS `int 0x13/ah=0x42` 做 LBA 扩展读取。核心数据结构是 Disk Address Packet：

```asm
align 4
dap:
    db 0x10
    db 0
dap_count:
    dw 0
    dw 0x0000
dap_seg:
    dw KERNEL_HIGH_SEG
dap_lba:
    dq 1
```

当前内核从 LBA 1 开始，最多加载 767 个扇区，也就是 383.5 KiB：

```asm
%define KERNEL_SECTORS 767
```

Makefile 里也有同名概念：

```makefile
KERNEL_SECTORS := 767
```

这两个数必须一致。内核变大时要同步修改，否则镜像构建或启动会失败。

### 13.5 E820 内存探测

boot sector 用 BIOS `int 0x15/e820` 收集内存布局：

```asm
mov di, 0x500
xor ebx, ebx
mov edx, 0x534D4150
mov word [0x4F8], 0
```

结果放在低地址缓冲区，内核后面用它初始化 PMM。这里用的是 BIOS 接口，所以必须在进入保护模式前完成。

---

## 14. 保护模式切换

BuzzOS boot sector 进入保护模式的关键步骤：

1. 打开 A20。
2. 准备 GDT。
3. `lgdt [gdt_desc]`。
4. 设置 `CR0.PE`。
5. 远跳转刷新 `CS`。
6. 切到 `bits 32` 代码。
7. 加载数据段。
8. 设置 32 位栈。

对应代码：

```asm
; Enable A20
in al, 0x92
or al, 2
out 0x92, al

cli
lgdt [gdt_desc]
mov eax, cr0
or eax, 1
mov cr0, eax
jmp CODE_SEG:protected_start
```

远跳转：

```asm
jmp CODE_SEG:protected_start
```

这一步很关键。只设置 `CR0.PE` 不够，必须重新加载 `CS`，CPU 才会按保护模式段描述符解释后续指令。

进入 32 位后：

```asm
bits 32
protected_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x700000
```

### 14.1 为什么有 copy stub

BuzzOS 把内核先读到 `0x10000`，保护模式下再复制到 `0x100000`：

```asm
mov esi, 0x10000
mov edi, 0x100000
mov ecx, (KERNEL_SECTORS * 512) / 4
cld
rep movsd
mov eax, 0x100000
jmp eax
```

但是复制范围会覆盖 `0x7C00` 附近的 boot sector 自身。所以 bootloader 先把一小段 position-independent copy stub 复制到 `0x600`，再跳到 `0x600` 执行真正复制。

这是 bootloader 里最容易误改坏的地方之一。改这段时要确认：

- stub 不依赖被覆盖的地址。
- stub 所在地址不会被复制过程覆盖。
- `ECX` 是 dword 数，不是字节数。
- 最后跳转地址和 linker script 的内核入口一致。

---

## 15. 中断、异常和 IRQ 入口

中断入口在 [src/kernel/arch/i386/isr.asm](../src/kernel/arch/i386/isr.asm)。

### 15.1 异常有没有 error code

x86 异常分两类：

- CPU 自动压 error code。
- CPU 不压 error code。

为了让 C 处理函数看到统一栈布局，BuzzOS 对无 error code 的异常手动压一个 0：

```asm
%macro EXC_NOERR 1
global exc_stub_%1
exc_stub_%1:
    push dword 0
    push dword %1
    jmp common_exception
%endmacro
```

有 error code 的异常，CPU 已经压了 error code，所以只压 vector：

```asm
%macro EXC_ERR 1
global exc_stub_%1
exc_stub_%1:
    push dword %1
    jmp common_exception
%endmacro
```

进入 `common_exception` 后，栈顶统一是：

```text
[esp + 0] = vector
[esp + 4] = error code
[esp + 8] = CPU frame...
```

### 15.2 `pusha` 后的寄存器布局

`pusha` 会保存通用寄存器。执行后 `ESP` 指向保存区开头，布局匹配：

```c
struct syscall_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};
```

所以 syscall stub 可以：

```asm
pusha
push esp
call syscall_handler
add esp, 4
popa
iret
```

C 代码修改 `frame->eax` 后，`popa` 会把新的 `EAX` 恢复到寄存器，用户态就拿到返回值。

注意：`popa` 会忽略保存区里的 `ESP` 槽位，不会真的把它 pop 到 `ESP`。所以修改 `frame->esp` 通常没有你想象的效果。

### 15.3 返回前为什么 `add esp, 8`

异常公共返回：

```asm
common_iret:
    popa
    add esp, 8
    iret
```

`add esp, 8` 是把前面统一压入的：

```text
vector
error code
```

弹掉。然后 `iret` 才能看到 CPU 原本压入的返回 frame。

如果忘了 `add esp, 8`，`iret` 会把 vector 当成 EIP，基本必炸。

### 15.4 IRQ 和 EOI

键盘 IRQ：

```asm
irq_stub_33:
    pusha
    xor eax, eax
    in al, 0x60
    push eax
    call keyboard_handler
    add esp, 4
    mov al, 0x20
    out 0x20, al
    popa
    iret
```

关键点：

- 从端口 `0x60` 读 scancode。
- 调 C handler。
- 给 PIC 主片端口 `0x20` 发送 EOI。
- `iret` 返回。

如果忘记 EOI，PIC 可能不再继续发送后续同级中断。

Timer IRQ 当前先发 EOI，再调用 `timer_irq`：

```asm
irq_stub_32:
    pusha
    mov al, 0x20
    out 0x20, al
    call timer_irq
    popa
    iret
```

原因是 `timer_irq` 里可能触发较长的调度路径，提前 EOI 可以避免阻塞后续 timer interrupt。

---

## 16. syscall 入口

用户态执行：

```asm
int 0x80
```

IDT 把它送到：

```asm
global syscall_stub
syscall_stub:
    pusha
    push esp
    call syscall_handler
    add esp, 4
    popa
    iret
```

IDT 里 `0x80` gate 的 DPL 是 3，所以用户态允许调用：

```c
{SYSCALL_VECTOR, syscall_stub, IDT_GATE_INT_USER},
```

`syscall_handler` 做 dispatch：

```c
uint32_t eax = frame->eax;
uint32_t ebx = frame->ebx;
uint32_t ecx = frame->ecx;
uint32_t edx = frame->edx;
uint32_t esi = frame->esi;
uint32_t edi = frame->edi;

int nr = (int)eax;
int result = -1;
if (nr < 256 && syscall_table[nr])
    result = syscall_table[nr](ebx, ecx, edx, esi, edi);

frame->eax = (uint32_t)result;
```

这就是为什么用户程序把 syscall number 放 `EAX`，把参数放 `EBX/ECX/EDX/ESI/EDI`。

### 16.1 加一个 syscall 的汇编视角

假设要加：

```c
int uptime_ms(void);
```

需要：

1. 在 syscall enum 里分配一个号。
2. 在内核注册 `sys_uptime_ms`。
3. 用户态 wrapper 用 `int 0x80`。

汇编用户程序可以直接：

```asm
%define SYS_UPTIME_MS 43

mov eax, SYS_UPTIME_MS
int 0x80
; eax = uptime_ms()
```

如果 syscall 需要 4 个参数，第 4 个放 `ESI`。需要 5 个参数，第 5 个放 `EDI`。

---

## 17. 上下文切换

上下文切换在 [src/kernel/arch/i386/switch.asm](../src/kernel/arch/i386/switch.asm)：

```asm
global switch_context

switch_context:
    pushfd
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    mov eax, [esp + 36]
    mov [eax], esp

    mov esp, [esp + 40]

    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax
    popfd
    ret
```

C 声明是：

```c
void switch_context(uint32_t **old_esp_ptr, uint32_t *new_esp);
```

调用进入汇编时，栈大概是：

```text
[esp + 0]  = return address
[esp + 4]  = old_esp_ptr
[esp + 8]  = new_esp
```

汇编先压了 8 个 dword：

```text
pushfd + 7 个寄存器 = 32 bytes
```

所以参数位置变成：

```text
[esp + 36] = old_esp_ptr
[esp + 40] = new_esp
```

切换逻辑：

1. 保存当前寄存器和 EFLAGS 到当前栈。
2. 把当前 `ESP` 写入 `*old_esp_ptr`。
3. 把 `ESP` 改成下一个 task 的 saved stack。
4. 从新栈恢复寄存器和 EFLAGS。
5. `ret` 到新 task 上次暂停的位置。

### 17.1 新 task 的栈必须匹配恢复顺序

`switch_context` 恢复时假定新栈布局是：

```text
[esp + 0]  = edi
[esp + 4]  = esi
[esp + 8]  = ebp
[esp + 12] = ebx
[esp + 16] = edx
[esp + 20] = ecx
[esp + 24] = eax
[esp + 28] = eflags
[esp + 32] = return address for ret
```

如果 `task.c` 构造新 task 栈时顺序不一致，第一次切过去就会跳飞。

这类 bug 常见表现：

- `EIP=0x00000000`
- `#UD`
- `#GP`
- QEMU 直接 reset

---

## 18. setjmp 和 longjmp

[src/kernel/arch/i386/setjmp.asm](../src/kernel/arch/i386/setjmp.asm) 实现了最小内核版 setjmp/longjmp。

保存内容：

```text
jmp_buf layout: [ebx, esi, edi, ebp, esp, eip]
```

`kernel_setjmp`：

```asm
kernel_setjmp:
    mov eax, [esp + 4]
    mov [eax + 0],  ebx
    mov [eax + 4],  esi
    mov [eax + 8],  edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]
    mov [eax + 16], ecx
    mov ecx, [esp]
    mov [eax + 20], ecx
    xor eax, eax
    ret
```

它保存 callee-saved 寄存器、调用者栈位置、返回地址。第一次返回 `0`。

`kernel_longjmp`：

```asm
kernel_longjmp:
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    test eax, eax
    jnz .nonzero
    inc eax
.nonzero:
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]
```

它恢复上下文，并跳到之前保存的返回地址，让 `kernel_setjmp` 看起来第二次返回，返回值是非 0。

注意：这不是完整 libc setjmp。它只保存 BuzzOS 当前需要的最小状态。

---

## 19. 调试汇编代码

### 19.1 看串口日志

运行时用：

```powershell
make run
```

QEMU 参数里有：

```text
-serial stdio
```

内核串口日志会输出到当前终端。汇编导致崩溃时，串口日志通常比 VGA 更可靠。

### 19.2 看异常日志

异常处理会打印：

```text
Vector:
Error:
EIP=
CS=
EFLAGS=
ESP=
SS=
EAX=
```

判断方向：

| 现象 | 常见原因 |
| --- | --- |
| `#UD` | EIP 跳到垃圾地址，或真的执行了非法指令 |
| `#GP` | 段选择子、权限、`iret` frame、栈切换错误 |
| `#PF` | 访问未映射地址或权限错误 |
| `EIP=0` | 返回地址坏了，函数指针为空，或 task 栈构造错 |
| `CS=0x08` | 内核态出错 |
| `CS=0x1B` | 用户态出错 |

### 19.3 反汇编 ELF

可以用 LLVM 工具看内核或用户程序：

```powershell
llvm-objdump -d build/user/asmhello.elf
llvm-objdump -d build/obj/kernel/kernel.elf
```

反汇编能确认：

- `_start` 地址是否在 `0x200000` 附近。
- 函数符号是否存在。
- 指令编码是否符合预期。
- 链接器有没有把段放错。

### 19.4 生成 map 文件

如果要长期调汇编，建议给内核链接增加 map：

```makefile
$(LD) $(LDFLAGS) -Map=$(OBJDIR)/kernel.map -o $@ $(KERNEL_OBJS)
```

map 文件可以告诉你每个符号最终地址，和异常里的 `EIP` 对起来非常有用。

### 19.5 用 `hlt` 停住

内核态调试时可以临时写：

```asm
cli
.halt:
    hlt
    jmp .halt
```

用户态不要执行 `hlt`。`hlt` 是特权指令，用户态执行会触发 `#GP`。

---

## 20. 常见错误清单

### 20.1 忘记设置 `bits 32`

ELF32 内核汇编或用户汇编忘了：

```asm
bits 32
```

可能导致指令编码不符合预期。

### 20.2 在 ELF32 汇编里乱用 `org`

`org` 是 raw binary 思维。内核和用户程序由链接器决定地址，应该依赖 linker script 和 section。

### 20.3 没保存 callee-saved 寄存器

汇编函数如果被 C 调用，改了这些寄存器要恢复：

```text
EBX ESI EDI EBP
```

否则调用者的 C 代码可能在很远的地方崩。

### 20.4 syscall 字符串没以 0 结尾

路径必须是 C 字符串：

```asm
path db "/fs/a.txt", 0
```

少了结尾 0，内核 `user_string_ok` 会失败，或读到错误内容。

### 20.5 `write` 长度算错

推荐：

```asm
msg db "hello", 10
msg_len equ $ - msg
```

不要手算长度。

### 20.6 栈清理不对

从汇编调用 C 函数后，cdecl 需要调用者清理参数：

```asm
push arg2
push arg1
call func
add esp, 8
```

忘记 `add esp, 8` 会让后续 `ret` 用错返回地址。

### 20.7 IRQ 忘记 EOI

硬件 IRQ handler 结束前通常要：

```asm
mov al, 0x20
out 0x20, al
```

如果是 slave PIC 的 IRQ，还要给 `0xA0` 和 `0x20` 都发 EOI。BuzzOS 当前键盘和 timer 都在 master PIC。

### 20.8 `iret` frame 顺序错

从内核进入用户态时，`iret` 需要的栈布局是：

```text
SS
ESP
EFLAGS
CS
EIP
```

因为 `push` 后栈向下长，所以代码里要按这个顺序压：

```asm
push user_ss
push user_esp
push eflags
push user_cs
push user_eip
iret
```

顺序错了通常是 `#GP`。

### 20.9 用户态执行特权指令

这些只能内核态用：

```text
hlt cli sti lgdt lidt ltr mov cr0/cr3 in out
```

用户态执行会触发异常。

### 20.10 修改 boot sector 超过 512 字节

boot sector 必须：

```text
大小 = 512 bytes
末尾 = 0x55AA
```

加功能时如果空间不够，不要硬塞。应该把更多逻辑放进二阶段 loader 或内核。

---

## 21. 练习路线

按这个顺序练，收益最高。

### 21.1 用户态练习

1. 写 `/asmhello`，用 `SYS_WRITE` 打印一行。
2. 写 `/asmargs`，打印 `argc` 和 `argv[0]`。
3. 写 `/asmfile`，创建 `/fs/asm.txt` 并写入内容。
4. 写 `/asmpid`，调用 `SYS_GETPID` 和 `SYS_GETTID`，把数字转成字符串输出。
5. 写 `/asmsleep`，调用 `SYS_SLEEP`，观察 shell 前台等待行为。

这些练习只需要用户态权限，不容易把系统整体打崩。

### 21.2 内核汇编练习

1. 添加 `asm_add3`，从 `kernel.c` 调用并用串口打印结果。
2. 添加 `read_eflags` 汇编函数，返回当前 EFLAGS。
3. 添加 `read_cr3` 汇编函数，确认 task 切换前后的地址空间。
4. 在 `syscall_stub` 临时统计进入次数，注意保存寄存器。
5. 给一个未使用 IRQ 写最小 stub，理解 PIC EOI。

做内核练习时，每次只改一个点。先确认能编译，再确认能启动，再确认行为。

### 21.3 Bootloader 练习

1. 修改 `loading_msg`，确认 BIOS 打印路径。
2. 在磁盘读取失败路径打印更多字符。
3. 理解 `dap` 并改变单次读取 chunk 大小。
4. 同步修改 `KERNEL_SECTORS`，观察构建脚本对内核大小的检查。
5. 不建议一开始就重写保护模式切换。先读懂，再小改。

---

## 附录 A. BuzzOS syscall 号

当前 syscall 号定义在 [src/kernel/syscall/syscall.h](../src/kernel/syscall/syscall.h) 和 [src/user/libc/libc.c](../src/user/libc/libc.c)。

| 号 | 名称 |
| --- | --- |
| `1` | `SYS_EXIT` |
| `2` | `SYS_OPEN` |
| `3` | `SYS_CLOSE` |
| `4` | `SYS_READ` |
| `5` | `SYS_WRITE` |
| `6` | `SYS_SPAWN` |
| `7` | `SYS_YIELD` |
| `8` | `SYS_JOIN` |
| `9` | `SYS_SLEEP` |
| `10` | `SYS_KILL` |
| `11` | `SYS_GETPID` |
| `12` | `SYS_GETTID` |
| `13` | `SYS_CHDIR` |
| `14` | `SYS_GETCWD` |
| `15` | `SYS_WAITPID` |
| `16` | `SYS_DUP` |
| `17` | `SYS_DUP2` |
| `18` | `SYS_STAT` |
| `19` | `SYS_GETDENTS` |
| `20` | `SYS_SPAWN_PROC` |
| `21` | `SYS_PS` |
| `22` | `SYS_REBOOT` |
| `23` | `SYS_MKDIR` |
| `24` | `SYS_UNLINK` |
| `25` | `SYS_CREATE` |
| `26` | `SYS_SPAWN_PROC_ARGS` |
| `27` | `SYS_LSEEK` |
| `28` | `SYS_RMDIR` |
| `29` | `SYS_RENAME` |
| `30` | `SYS_SOCKET` |
| `31` | `SYS_CONNECT` |
| `32` | `SYS_SEND` |
| `33` | `SYS_RECV` |
| `34` | `SYS_CLOSESOCKET` |
| `35` | `SYS_DNS_RESOLVE` |
| `36` | `SYS_BIND` |
| `37` | `SYS_SENDTO` |
| `38` | `SYS_RECVFROM` |
| `39` | `SYS_NETINFO` |
| `40` | `SYS_PIPE` |
| `41` | `SYS_FUTEX_WAIT` |
| `42` | `SYS_FUTEX_WAKE` |
| `44` | `SYS_GFX_CLEAR` |
| `45` | `SYS_GFX_PUTPIXEL` |
| `46` | `SYS_GFX_FILL_RECT` |
| `47` | `SYS_GFX_TEXT` |
| `48` | `SYS_FB_BLIT` |
| `49` | `SYS_MOUSE_GET` |

调用寄存器：

| 参数 | 寄存器 |
| --- | --- |
| syscall number | `EAX` |
| arg1 | `EBX` |
| arg2 | `ECX` |
| arg3 | `EDX` |
| arg4 | `ESI` |
| arg5 | `EDI` |
| return | `EAX` |

---

## 附录 B. 常用选择子和地址

| 项 | 值 |
| --- | --- |
| boot sector 加载地址 | `0x7C00` |
| 内核临时读取地址 | `0x10000` |
| 内核最终地址 | `0x100000` |
| boot 阶段栈 | `0x7C00` |
| 保护模式早期栈 | `0x700000` |
| 用户程序链接地址 | `0x200000` |
| 用户 trampoline | `0x1FF000` |
| 用户栈顶 | `0x27F000` |
| 用户地址范围 | `0x001C0000..0x00280000` |
| 内核代码选择子 | `0x08` |
| 内核数据选择子 | `0x10` |
| 用户代码选择子 | `0x1B` |
| 用户数据选择子 | `0x23` |
| TSS 选择子 | `0x28` |
| syscall vector | `0x80` |
| legacy syscall vector | `0x30` |
| timer IRQ vector | `32` |
| keyboard IRQ vector | `33` |
| keyboard data port | `0x60` |
| PIC master command | `0x20` |

---

## 附录 C. 常用指令速查

### 数据移动

```asm
mov eax, ebx
mov eax, [addr]
mov [addr], eax
lea eax, [ebx + ecx * 4]
```

### 栈

```asm
push eax
pop eax
pushfd
popfd
pusha
popa
```

### 分支

```asm
cmp eax, ebx
je equal
jne not_equal
jl signed_less
jb unsigned_less
jmp target
call func
ret
iret
```

### 字符串复制

```asm
cld
mov esi, src
mov edi, dst
mov ecx, count_dwords
rep movsd
```

### 端口 IO

```asm
in al, 0x60
out 0x20, al
```

端口 IO 是特权操作，只能内核态使用。

### 控制寄存器

```asm
mov eax, cr0
or eax, 1
mov cr0, eax

mov eax, cr3
mov cr3, eax
```

控制寄存器也是特权操作。

### 中断

```asm
int 0x80
```

用户态只应该使用被 IDT 明确允许的中断门。BuzzOS 当前允许用户态调用 `0x80`，也保留了 legacy `0x30`。

---

## 最后建议

学 BuzzOS 汇编不要从“重写 bootloader”开始。更稳的路线是：

1. 先写纯汇编用户程序，熟悉 `int 0x80`。
2. 再写 C 可调用的内核汇编函数，熟悉 cdecl 和链接。
3. 然后读 `isr.asm`，理解中断栈和 `iret`。
4. 再读 `switch.asm`，理解 task 栈布局。
5. 最后再改 `boot.asm`，因为 boot sector 空间小、状态少、失败时日志也少。

这样你会先掌握可验证的局部知识，再处理最脆弱的启动阶段。
