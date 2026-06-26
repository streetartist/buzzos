# 文档目录

这里放的是手把手教程，不只是项目说明。

## 阅读顺序

- 第一次来：[教程与原理](tutorial.md) — 一篇从 0 到跑起来的极致详细教程。
  - 0. 仓库目标与启动链速览
  - 1. 准备开发环境
  - 2. 仓库文件地图与角色
  - 3. 启动链整体原理（BIOS → C 内核）
  - 4. BIOS 把控制权交给启动扇区
  - 5. 启动扇区把后续内核读进内存
  - 6. 进入 32 位保护模式
  - 7. 跳到 C 语言内核入口
  - 8. VGA 文本显存写屏
  - 9. 清零 `.bss` 的意义与做法
  - 10. 构建管线全景（Makefile 每一行在做什么）
  - 11. 亲手做三个改动并验证
  - 12. 出错了怎么排查
  - 13. 下一步要补的能力（按推荐顺序）
  - 15. 串口（COM1）输出（NEW）
  - 16. GDT 搬进 C（NEW）
  - 18. 图形桌面、VGA framebuffer syscall、鼠标输入和 `/fs/apps` 用户 GUI 程序
  - 附录 A-C：BIOS 初始寄存器、常用端口、QEMU 参数
- 想先在本地跑起来：[本地启动与引导指南](boot-guide.md) — 从仓库目录、doctor、构建、可输入 QEMU、GUI 快捷入口到常见启动问题。
- 已经进入 BuzzOS：[用户指南](user-guide.md) — shell、GUI、文本输入框、`/fs`、`/proc`、网络、IPC 和排错小抄。
- 想写或改汇编：[BuzzOS 汇编编程教程](assembly-programming.md) — 从用户态 `int 0x80` 程序到 boot sector、ISR、syscall stub 和上下文切换。
  - 用户态纯汇编 ELF 程序
  - 在 BuzzOS 内用 `nano` + `basm` 编辑、汇编、运行简单程序
  - syscall ABI：`EAX/EBX/ECX/EDX/ESI/EDI`
  - 内核汇编函数和 C 调用约定
  - boot sector、保护模式、中断入口、IRQ EOI
  - `switch_context`、`setjmp/longjmp` 和汇编调试
- 想看当前项目成熟度、验证方式和下一步路线：[Project Status](project-status.md)。
- 想看用户态 GUI 和 `/fs/apps` app 示例：[User GUI Example](user-gui.md)。
- 想看阶段性项目日志和本轮成熟化记录：[Changelog](../CHANGELOG.md)。

如果你只是想快速看项目入口，再回到仓库根目录的 [README.md](../README.md)。
