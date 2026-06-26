# BuzzOS 用户指南

这份指南从进入 BuzzOS shell 后开始，说明怎么使用文本 shell、GUI、用户 GUI 示例、`/fs`、`/proc` 和常见诊断命令。启动前的宿主机步骤见 [boot-guide.md](boot-guide.md)。

## 1. 进入系统

启动完成后会看到：

```text
=== BuzzOS User Shell ===
buzzos:/>
```

先试这组命令：

```text
help
about
health
interfaces
limits
fsinfo
ls /
ls /proc
ls /fs/apps
```

`about` 是项目介绍，`health` 是一屏状态，`interfaces` 是能力矩阵，`limits` 是容量边界，`fsinfo` 是 `/fs` 文件系统状态。

## 2. Shell 基本操作

常用文件命令：

```text
pwd
ls /fs
mkdir /fs/demo
write /fs/demo/hello hello
cat /fs/demo/hello
stat /fs/demo/hello
mv /fs/demo/hello /fs/demo/msg
rm /fs/demo/msg
rmdir /fs/demo
```

Shell 支持左右方向键、Home、End、Delete、Backspace、上下历史和 `Ctrl+C`。查看帮助主题：

```text
help apps
help gui
help files
help proc
help edit
help net
help pipes
```

管道和重定向示例：

```text
echo hello | cat | cat
echo saved > /fs/out
echo again >> /fs/out
cat < /fs/out
rm /fs/out
```

## 3. 运行 GUI

从 shell 输入：

```text
gui
```

GUI 会进入 APP MANAGER。可用数字键或鼠标选择：

- `1` Paint
- `2` GUI Shell
- `3` Help
- `4` 或 `Enter` 运行当前选中的 `/fs/apps` 程序
- `Up/Down` 选择应用，`Left/Right` 滚动详情

鼠标滚轮在应用列表上滚动选择，在详情面板上滚动详情内容。

在 GUI 内，`Esc` 返回上一层；在管理器里再按 `Esc` 或 `Ctrl+C` 回到文本 shell。

GUI Shell 支持：

```text
help
about
health
limits
interfaces
fsinfo
ls
cat /proc/fs
apps
run /fs/apps/forms
```

## 4. 用户 GUI 示例

BuzzOS 默认把这些用户态 GUI 程序种子到 `/fs/apps`：

- `guidemo`：单行文本框、按钮、色块、鼠标输入和状态保存。
- `notes`：多行文本编辑器，保存到 `/fs/apps/notes.txt`。
- `forms`：多个文本框，支持鼠标聚焦、Tab/Enter 切换、光标编辑和实时预览。
- `calc`：两个输入框、运算按钮、键盘编辑和结果反馈。

从文本 shell 启动：

```text
guidemo
notes
forms
calc
```

也可以通过 app 模型查看：

```text
apps
apps info forms
apps run forms
```

文本框操作：

- 鼠标点击输入框聚焦。
- `Tab` 或 `Enter` 切换输入框。
- Backspace/Delete 删除字符。
- Left/Right/Home/End 移动光标。
- `Esc` 或 `Ctrl+C` 返回管理器。

## 5. `/proc` 状态文件

`/proc` 是只读运行状态，不占用 `/fs` 空间：

```text
cat /proc/about
cat /proc/health
cat /proc/interfaces
cat /proc/limits
cat /proc/fs
cat /proc/tasks
cat /proc/threads
cat /proc/meminfo
cat /proc/fds
cat /proc/net
cat /proc/sync
cat /proc/mounts
```

对应快捷命令：

```text
about
health
interfaces
limits
fsinfo
fdstat
netstat
syncstat
threads
```

`/proc/fs` 输出 `/fs` mount、minifs 状态、inode/block 用量、数据区 LBA、最大文件大小，以及宿主机检查/修复入口：

```text
cat /proc/fs
fsinfo
fsstat
```

`fsinfo` 读取 `/proc/fs`，适合看统一状态；`fsstat` 直接走文件系统 syscall，适合确认内核 minifs 计数。

## 6. 网络与 IPC 示例

QEMU user network 启动后会自动 DHCP。常用命令：

```text
netstat
ping 10.0.2.2
wget 10.0.2.2 8000
dhcp
```

IPC 和同步测试：

```text
pipetest
pipeedgetest
pipeblocktest
futextest
futextimeouttest
futexcanceltest
futexblocktest
```

`futexblocktest` 会展示 `/proc/threads` 和 `/proc/sync` 中的阻塞等待状态。

## 7. 在系统内写小程序

BuzzOS 带有 `nano` 和 `basm`：

```text
nano /fs/demo.asm
basm /fs/demo.asm /fs/demo
exec /fs/demo
```

在 `nano` 里：

- `Ctrl+T` 插入最小汇编模板。
- `Ctrl+S` 保存。
- `Ctrl+C` 退出。

`basm` 是教学用小 assembler，不是完整 NASM。它输出 BuzzOS loader 可执行的 ELF32 文件。

## 8. 排错小抄

看项目和系统状态：

```text
about
health
interfaces
limits
fsinfo
```

看进程、线程、fd、网络和同步：

```text
ps
threads
fdstat
netstat
syncstat
```

看文件系统：

```text
ls /fs
fsinfo
fsstat
cat /proc/fs
```

如果 GUI 里输入框不响应，先点击输入框，或按 `Tab` 切换焦点。若整个 QEMU 不吃键盘输入，回到宿主机按 [boot-guide.md](boot-guide.md) 的 QEMU 焦点步骤处理。
