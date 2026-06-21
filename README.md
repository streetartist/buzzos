# BuzzOS

一个最小可启动的 i386 操作系统骨架。目标是先跑通最小闭环，再一层一层把内存管理、进程、ELF、文件系统、图形、用户态补起来。

```text
BIOS -> boot sector -> protected mode -> C kernel -> VGA text output
```

跑起来后 QEMU 输出：

```text
BuzzOS
minimal i686 kernel

next: memory, syscalls, ELF, VFS, framebuffer
```

> **这个仓库是教学项目，不会停在"最小闭环"上。**
> 长期目标是一条从裸机到能在用户态跑 `gcc` 编译出来的 C 程序的完整路径。`docs/tutorial.md` 会按这条路径的每一个阶段补章节；每加一节能力，README 这里也会更新"已实现"清单。
> 现在还很薄——这正是它的形态，不是它的终点。

教程：[`docs/tutorial.md`](docs/tutorial.md)（中文，从工具链到 QEMU 验证共 14 章）
English：[README.en.md](README.en.md)

---

## 已实现 / 计划中

- [x] BIOS 启动扇区签名校验
- [x] 启动扇区从磁盘读内核到 `0x1000`
- [x] A20 打开
- [x] GDT + 32 位保护模式
- [x] freestanding C 内核入口
- [x] `.bss` 清零
- [x] VGA 文本模式输出
- [ ] 串口（COM1）输出
- [ ] IDT + 中断处理
- [ ] 物理内存管理（E820 + bitmap / 空闲链表）
- [ ] 分页 / 虚拟内存
- [ ] ELF 加载器
- [ ] 进程与调度
- [ ] 系统调用
- [ ] VFS（ramfs / devfs）
- [ ] framebuffer 图形输出
- [ ] 用户态 + 加载用户程序

清单每完成一项，对应教程章节就会加一节"这是怎么工作的"。详见 [`docs/tutorial.md`](docs/tutorial.md) §13。

---

## 构建与运行

工具链：

| 工具               | 用途                              | 必需 |
| ------------------ | --------------------------------- | ---- |
| `nasm`             | 汇编启动扇区                       | 必需 |
| `clang`            | 编译 freestanding C 内核           | 必需 |
| `ld.lld`           | 链接 ELF                          | 必需 |
| `llvm-objcopy`     | ELF → 纯二进制内核                 | 必需 |
| `make`             | 串起构建链                        | 必需 |
| `powershell`       | 拼镜像、清构建产物                 | Windows 必需；Linux / macOS 用 `dd` 替代 |
| `qemu-system-i386` | 启动镜像看效果                    | 强烈推荐 |

构建并启动：

```sh
make run
```

只要产物：

```sh
make
qemu-system-i386 -drive format=raw,file=build/buzzos.img
```

产物只有 `build/buzzos.img`（1 个启动扇区 + 64 个内核扇区 = 32 KiB + 512 B）。

工具链验证、命令排错、构建管线细节在 [`docs/tutorial.md`](docs/tutorial.md) §1、§10、§12。

---

## 仓库即教程

4 个核心文件，对应启动链的 4 跳：

- 启动扇区 — [`src/boot/boot.asm`](src/boot/boot.asm)
- C 内核入口 — [`src/kernel/kernel.c`](src/kernel/kernel.c)
- 链接脚本（装载地址、`.bss` 边界）— [`linker.ld`](linker.ld)
- 镜像打包 — [`tools/mkimage.ps1`](tools/mkimage.ps1)

读法建议：先按顺序读 [`docs/tutorial.md`](docs/tutorial.md)，每读一节回到对应文件对照代码；改完一节就跑一次 `make run` 看效果。教程里 §11 给出了三个练手改动（启动标题、内核文本、屏幕颜色），是验证整条链路最直接的入口。

---

## 贡献

最欢迎的贡献是**让教程和代码一起长大**：

1. **修教程里讲错的地方**：命令过时、术语不准、代码和解释对不上——直接提 PR 改 `docs/tutorial.md`。
2. **加一节能力**：在 §13 路线图里挑一项，按现有章节的格式写一节"它怎么工作、代码怎么改、怎么验证"。建议先开 issue 同步一下，避免多人撞车。
3. **加一个 demo 改动**：在 §11 的三个改动之外再加一个"改一行就看到效果"的小例子。

提 issue 也算贡献：跑不起来、某个步骤看不懂、对未来路线有意见——这些反馈直接影响教程下一节怎么写。

代码风格上保持每一节**短到一遍读完**，每一处改动**改完就能在 QEMU 里看到效果**。这个仓库的价值就在"小到能读完、活到能继续加"。
