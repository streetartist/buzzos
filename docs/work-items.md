# BuzzOS 工作项

这份清单来自本地源码阅读和 QEMU 冒烟验证。目标是把后续工作拆成可实现、可验收的小块，而不是只停在“下一步建议”。

## Done/P0: 加固 ELF loader

目标：让从 `/fs` 执行的 ELF 不能通过伪造 header 或 segment 写坏内核内存。

涉及文件：
- `src/kernel/core/elf.c`
- `src/kernel/core/elf.h`
- `src/kernel/core/exec.c`
- `src/kernel/syscall/sys_proc.c`

已完成：
- 把 `elf_load(const uint8_t *buf)` 改为接收 `elf_size`。
- 校验 ELF header 和 program header 表都在文件范围内。
- 校验 `e_phentsize == sizeof(struct elf32_phdr)`。
- 校验 `p_filesz <= p_memsz`。
- 校验 `p_offset + p_filesz` 不越过文件末尾。
- 校验所有 `PT_LOAD` 的 `[p_vaddr, p_vaddr + p_memsz)` 都落在用户加载窗口 `0x001C0000..0x00240000` 内。
- 校验 `e_entry` 落在至少一个可执行 `PT_LOAD` segment 内。
- 对整数加法做溢出检查。
- `exec` 遇到坏 ELF 会销毁临时用户页表并返回失败。
- shell 提供 `elfbadtest`，在 `/fs` 中构造坏 ELF 并验证 `exec` 返回失败。
- smoke 覆盖 `p_vaddr` 越界、`p_offset + p_filesz` 越界、`p_filesz > p_memsz` 和 entry 越界四类坏 ELF。
- `make check-project` 会检查 loader 加固点和 runtime smoke 覆盖。

验收标准：
- `make` 成功。
- QEMU 能启动到 shell。
- `/bin/sh`、`/hello`、`/bin/gui` 仍可执行。
- 构造一个 `p_vaddr` 越界的坏 ELF，`exec` 返回失败而不是触发异常。
- 构造一个 `p_offset + p_filesz` 越界的坏 ELF，`exec` 返回失败。

## P0: 修正 PMM 与分页映射边界

目标：避免 `pmm_alloc_pages()` 返回内核当前不能直接访问的物理地址。

涉及文件：
- `src/kernel/mm/pmm.c`
- `src/kernel/arch/i386/paging.c`
- `src/kernel/arch/i386/paging.h`

工作内容：
- 明确当前内核 direct-map 范围，目前是低 8 MiB。
- 短期方案：PMM 只把低 8 MiB 中可安全使用的页标记为 free。
- 或者长期方案：为内核建立更大的 direct map，再允许 PMM 使用更多物理页。
- 给 PMM 增加 debug 统计，输出可分配上限和已保留区间。

验收标准：
- 多次启动、执行 `/hello`、`pipetest`、`futextest` 不出现 page fault。
- 连续创建多个后台进程或线程，不会因为高物理页直接访问而崩溃。
- serial 日志能清楚显示 PMM 可管理范围。

## P1: syscall 中断与锁策略

目标：减少长 syscall 导致的输入延迟和调度停顿。

涉及文件：
- `src/kernel/arch/i386/idt.c`
- `src/kernel/arch/i386/isr.asm`
- `src/kernel/syscall/syscall.c`
- `src/kernel/fs/vfs.c`
- `src/kernel/fs/minifs/minifs.c`
- `src/kernel/block/cache.c`

工作内容：
- 评估把 `int 0x80` 从 interrupt gate 改为 trap gate。
- 明确哪些内核临界区需要关中断，哪些只需要自旋锁。
- 避免持有 VFS/minifs/cache 锁时做长时间等待。
- 给 syscall 入口和返回增加可选 trace，方便观察耗时路径。

验收标准：
- shell 输入在执行文件操作或网络等待时仍有可接受响应。
- timer tick 不会被普通 syscall 长时间屏蔽。
- QEMU 冒烟测试仍通过。

## Done/P1: futex 接入 scheduler wait queue

目标：让 `futex_wait` 不再靠 yield 轮询。

涉及文件：
- `src/kernel/syscall/sys_ipc.c`
- `src/kernel/sched/task.c`
- `src/kernel/sched/task.h`

已完成：
- 给 task 增加 blocked/waiting 状态或等待原因。
- `futex_wait(addr, expected)` 在值匹配时把当前 task 挂到等待队列。
- `futex_wake(addr, count)` 唤醒匹配地址的等待 task。
- 处理 task exit/kill 时清理 futex waiter。
- `futex_wait_timeout` 使用 scheduler deadline，超时返回 `-2`。
- `/proc/sync` 和 `syncstat` 暴露 futex waiter 表。
- `futexblocktest` 在 `/proc/threads` 中验证等待线程处于 `BLOCKED`。
- `futexcanceltest` 验证 kill/exit 清理 waiter 后槽位可复用。
- `make check-project` 会检查 futex 使用 `task_block_current(_until)` 和 `task_wake`，并确认 smoke 覆盖。

验收标准：
- `futextest` 仍通过。
- 等待中的 task 在 `ps` 中能显示为睡眠或阻塞状态。
- futex wait 不再持续占用调度轮转。

## Done/P1: pipe 阻塞语义和 shell 管道

目标：把现有 pipe 从“短读写测试接口”推进到更接近 POSIX 的阻塞语义，并让 shell 管道/重定向成为可日常使用的外部程序组合能力。

涉及文件：
- `src/kernel/fs/pipefs.c`
- `src/kernel/fs/vfs.c`
- `src/user/bin/shell.c`
- `src/user/libc/libc.c`
- `src/user/libc/libc.h`

已完成：
- shell 支持 `cmd1 | cmd2 | cmd3` 这类多段外部程序管道。
- shell 支持 `<`、`>`、`>>` 基础重定向。
- 管道/重定向 spawn 使用 `SPAWN_FLAG_INHERIT_STDIO`，只继承 fd 0/1/2，避免临时 pipe 端泄漏给子进程。
- initrd 内置 `/bin/echo` 和 `/bin/cat`。
- smoke 覆盖 `echo pipelinesmokeok | cat`。
- smoke 覆盖 `echo multipipesmokeok | cat | cat`。
- smoke 覆盖 `echo redir-one > /fs/redir`、`echo redir-two >> /fs/redir` 和 `cat < /fs/redir`。
- 空 pipe 且 writer 存在时，read 会阻塞并在写入后被唤醒。
- 满 pipe 且 reader 存在时，write 会阻塞并在读端腾出空间后继续。
- `pipeedgetest` 覆盖 EOF 和 closed-reader 边界。
- `pipeblocktest` 覆盖 reader wakeup 和 full-buffer writer wakeup。
- `make check-project` 会检查 pipe waiter 状态、阻塞/唤醒路径和 smoke 覆盖。

剩余方向：
- 增加 shell 引号、转义和环境变量展开。
- 增加更完整的后台任务/job control 体验。
- 后续如果加入 `O_NONBLOCK`，再定义非阻塞 pipe 的错误码。

验收标准：
- `pipetest` 仍通过。
- `echo hello | cat` 这类简单管道路径可运行。
- `echo hello | cat | cat`、`>`、`>>`、`<` 路径可运行。
- `pipeedgetest` 和 `pipeblocktest` 在 smoke 中稳定通过。

## Done/P1: 清理 initrd 生成物漂移

目标：减少 `src/kernel/initrd.h` 因工具链版本变化产生的大型无意义 diff。

涉及文件：
- `Makefile`
- `tools/mkinitrd.py`
- `src/kernel/initrd.h`

已完成：
- 用户 ELF 链接后统一执行 `llvm-objcopy --strip-sections`。
- `build/user/*.elf` 的 section header 表为 0，不再携带 `.comment`、`.symtab`、`.strtab` 等非运行必要 section。
- `tools/mkinitrd.py` 使用 32 字节一行输出，减少生成头文件行数和 diff 噪声。
- 保留 `PT_LOAD` 需要的内容，确保 loader 仍能执行。
- 记录本地重生成 initrd 的命令。
- `make check-project` 会检查用户 ELF strip 状态和 mkinitrd 行宽。

验收标准：
- `make` 成功。
- 用户 ELF 中不再嵌入 LLVM `.comment` 字符串和 section header 表。
- `src/kernel/initrd.h` 使用紧凑行宽生成。
- QEMU 启动和 shell 命令测试通过。

## Done/P2: TCP per-socket PCB

状态：基础拆分已完成。`socket_entry` 现在持有 per-socket TCP PCB，
`connect/send/recv/close` 通过 PCB API 操作连接状态；网络层也有轻量
TCP 输入 demux 和 per-PCB 接收缓冲。后续重点是乱序/重复包处理、更大
的 recv buffer、超时、重传和多连接回归测试。

目标：把当前全局 TCP 状态拆成每个 socket 独立状态，为并发连接做准备。

涉及文件：
- `src/kernel/net/net.c`
- `src/kernel/syscall/sys_net.c`
- `src/kernel/net/net.h`

工作内容：
- 定义 TCP PCB：本地端口、远端 IP/端口、seq/ack、状态、缓冲区。
- `socket_entry` 关联 PCB，而不是依赖单个 `active_tcp_socket`。
- `connect/send/recv/close` 按 socket 操作对应 PCB。
- 逐步支持多个 TCP client socket。
- TCP 入站帧先按 PCB 分发，避免被无关 socket 直接读走。
- 每个 PCB 有小接收缓冲，`/proc/net` 暴露 buffered/dropped 计数。
- `make smoke` 启动宿主机本地 TCP HTTP 服务，并用 BuzzOS `wget 10.0.2.2 <port>` 覆盖 socket connect/send/recv。
- `make smoke` 还用 `tcptwotest` 同时打开两个 TCP socket，按 B 再 A 的读取顺序验证 demux/缓冲。

验收标准：
- 现有 `wget <host>` 仍可用。
- 两个 TCP socket 不互相覆盖状态。
- TCP 输入 demux 不把匹配其他 PCB 的数据包直接丢弃。
- QEMU smoke 能稳定验证一次本地 TCP socket HTTP 拉取。
- QEMU smoke 能稳定验证两个 TCP socket 的响应不会互相覆盖或丢失。
- 失败连接能释放 PCB。

## Done/P2: 轻量多接口状态面

目标：让用户、GUI、脚本和报告从同一套轻量文本接口了解系统健康度与当前可用入口。

涉及文件：
- `src/kernel/fs/procfs.c`
- `src/user/bin/shell.c`
- `src/user/bin/gui.c`
- `scripts/smoke.ps1`
- `tools/check_project.py`
- `tools/project_report.py`
- `docs/procfs.md`

已完成：
- 增加 `/proc/about`，用轻量键值文本暴露项目名称、定位、架构、入口、文档和变更日志位置。
- 文本 shell 增加 `about` 命令。
- GUI shell 增加 `about` 命令。
- 增加 `/proc/health`，聚合内存页、`/fs` 使用情况、网络 IP、procfs 条目数和接口面说明。
- 文本 shell 增加 `health` 命令。
- GUI shell 增加 `health` 命令。
- 增加 `/proc/interfaces`，用 `NAME STATUS ENTRYPOINTS` 表描述 procfs、health、apps、shell、GUI、fs、net、sync 和 report 等入口。
- 文本 shell 增加 `interfaces` 命令。
- GUI shell 增加 `interfaces` 命令。
- `make report` 输出 Project Identity、Health Interfaces 和 Runtime Interfaces。
- `make check-project` 静态检查 procfs、文本 shell、GUI shell、报告和 smoke 覆盖。
- `make smoke` 在 QEMU 中实际读取 `/proc/about`、`/proc/health`、`/proc/interfaces` 并执行 `about`、`health`、`interfaces`。

验收标准：
- `cat /proc/about` 输出 `name BuzzOS`、项目定位、入口、文档和 `CHANGELOG.md`。
- `about` 与 `/proc/about` 走同一数据源。
- `cat /proc/health` 输出 `status ok`、`fs_status ok`、`net_ip` 和 `proc_entries`。
- `health` 与 `/proc/health` 走同一数据源。
- `cat /proc/interfaces` 输出稳定/实验性入口矩阵。
- `interfaces` 与 `/proc/interfaces` 走同一数据源。
- `make verify` 通过。

## P2: minifs 校验与恢复工具

目标：让 `/fs` 在反复写删、异常退出后更容易诊断和修复。

涉及文件：
- `src/kernel/fs/minifs/minifs.c`
- `src/kernel/fs/minifs/minifs.h`
- `src/user/bin/shell.c`

工作内容：
- 增加 inode、bitmap、目录项一致性检查函数。
- shell 增加 `fsck` 或只读 `fsstat` 命令。
- 检查孤儿 inode、重复 block、越界 block、坏 parent。
- 可选：提供只读报告先于自动修复。

验收标准：
- `fsstat` 能输出 inode/block 使用情况。
- 人为损坏镜像某些元数据时，能报告错误而不是静默失败。

## P2: `/proc` 伪文件系统

目标：用文件系统暴露任务、内存、网络状态，减少专用调试命令。

涉及文件：
- `src/kernel/fs/vfs.c`
- `src/kernel/fs/procfs.c`
- `src/kernel/sched/task.c`
- `src/kernel/mm/pmm.c`
- `src/kernel/net/net.c`

工作内容：
- 增加 `/proc` mount。
- 提供 `/proc/tasks`、`/proc/meminfo`、`/proc/net`。
- 让 shell 通过 `cat /proc/tasks` 查看状态。

验收标准：
- `ls /proc` 可列出伪文件。
- `cat /proc/tasks` 输出与 `ps` 基本一致。
- 不需要落盘，不影响 `/fs`。

## P2: 自动化 QEMU 冒烟测试

目标：把当前手动验证固化成脚本，方便每次改内核后快速确认。

涉及文件：
- `scripts/`
- `Makefile`

工作内容：
- 新增 headless QEMU 启动脚本。
- 使用 monitor `sendkey` 注入命令。
- 检查 serial log 中的 shell prompt 和关键输出。
- 覆盖 `ls /`、`ls /bin`、`write/cat/rm /fs/smoke`、`pipetest`、`futextest`、`ps`。

验收标准：
- 一条命令可以运行 smoke test。
- 测试结束后不留下 QEMU 进程。
- 失败时输出 serial log tail。

## 建议顺序

1. P0 PMM/分页边界继续审计。
2. P2 minifs 校验。
3. P2 `/proc`。
4. P2 TCP 多连接 demux / buffer / timeout / retransmit。
5. Shell 引号、环境变量展开和 job control。
