# Project Status

BuzzOS is a lightweight educational i386 operating system. The project is now
organized around a small kernel, user-space programs, a persistent mini
filesystem, and a user-space GUI app manager.

## Current Shape

- Boot: 16-bit BIOS sector loads a protected-mode 32-bit kernel.
- Kernel: GDT/IDT, paging, preemptive scheduling, syscalls, VFS, ATA, minifs,
  NE2000 networking, keyboard, mouse, VGA text, and VGA 13h graphics syscalls.
- User space: `/bin/sh`, `/bin/echo`, `/bin/cat`, `nano`, `basm`, `/bin/gui`, and seeded GUI apps under
  `/fs/apps`.
- Pseudo filesystems: `/dev`, persistent `/fs`, and read-only `/proc` status
  files, including process, thread, network, memory, mount, and sync views.
- Health interface: `/proc/health`, text-shell `health`, GUI-shell `health`,
  and `make report` all expose the same compact project health surface.
- Interface matrix: `/proc/interfaces`, text-shell `interfaces`, GUI-shell
  `interfaces`, and `make report` document the current stable/experimental
  entrypoints without adding a heavier registry service.
- GUI examples: `guidemo`, `notes`, `forms`, and `calc`.
- App packaging: optional `.app` manifests provide `name`, `kind`, `version`,
  `summary`, `state`, `source`, and `readme` metadata for the App Center.
- App registry: `tools/gen_app_registry.py` generates `src/kernel/app_registry.h`
  from app sidecar metadata so kernel seeding stays data-driven.
- Initrd hygiene: user ELF payloads are section-stripped before embedding, and
  `tools/mkinitrd.py` emits compact 32-byte rows to reduce generated diff noise.
- App management: the text shell also exposes `apps`, `apps info <name>`, and
  `apps run <name>`.
- Shell execution: external commands support multi-stage pipelines plus
  `<`, `>`, and `>>` redirection through stdio-only fd inheritance, so temporary
  pipe endpoints do not leak into child processes.
- ELF loader: user executables are size-aware validated before loading; malformed
  headers, out-of-file segments, out-of-window `PT_LOAD` ranges, and invalid
  entry points are rejected before memory writes.
- In-system help: `help apps`, `help gui`, `help files`, `help proc`,
  `help edit`, and `help net` describe common workflows from inside BuzzOS.
- Persistence: `/fs` is preserved across normal image rebuilds.
- Minifs diagnostics: `tools/check_minifs.py` validates the on-image `/fs`
  superblock, inode table, block bitmap, directory tree, and block references.
- In-OS diagnostics: the shell `fsstat` command reports current `/fs` inode and
  block usage from kernel minifs metadata.
- Scheduler diagnostics: `/proc/threads` and the `threads` shell command show
  individual TID/PID state, making blocked futex waits visible.
- Network diagnostics: `/proc/net` and `netstat` show MAC/IP, DHCP state,
  gateway/DNS defaults, TCP state, frame counters, and per-protocol counters.
- Stream sockets carry per-socket TCP PCBs with lightweight input demux and
  small per-PCB receive buffers, so TCP connection state is no longer gated by
  one global active socket.
- The `wget` shell command supports `wget <host> [port]`, including direct IPv4
  targets for local QEMU user-network tests.
- The `tcptwotest` shell command opens two stream sockets and reads them in
  reverse order, exercising TCP PCB demux and receive buffering.
- Synchronization diagnostics: `/proc/sync` and `syncstat` show futex waiter
  table usage and active waiter slots.
- Futex waits are scheduler-backed: waiters enter `TASK_BLOCKED`, wakeups call
  back into the scheduler by futex address, and kill/exit paths clean waiter
  slots.
- FD diagnostics: `/proc/fds` and `fdstat` show the current fd-owner table,
  refs, flags, vnode kind/name, file positions, and pipe endpoint state.
- IPC diagnostics: `pipetest`, `pipeedgetest`, `pipeblocktest`, `futextest`,
  `futextimeouttest`, `futexcanceltest`, and `futexblocktest` cover normal,
  blocking, and edge behavior for pipe/futex primitives.

## Local Workflow

Build:

```sh
make
```

Run a visible QEMU session without stealing terminal input:

```sh
make run-local QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Start directly in the form-input GUI:

```sh
make run-forms QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Run the smoke test:

```sh
make smoke QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Run the fast host-side consistency check:

```sh
make check-project
```

Inspect the persistent mini filesystem:

```sh
make fs-check
make fs-ls
make fs-check-negative
make fs-check-repair
```

Inspect live kernel status from inside BuzzOS:

```text
help proc
ls /proc
cat /proc/tasks
cat /proc/threads
cat /proc/meminfo
cat /proc/health
cat /proc/interfaces
cat /proc/fds
cat /proc/net
cat /proc/sync
cat /proc/mounts
fdstat
health
interfaces
```

Validate only seeded GUI app packaging:

```sh
make app-registry
make app-check
python tools/check_project.py --list-apps
```

Run the GUI smoke test:

```sh
make gui-smoke QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Run all current checks:

```sh
make verify QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
```

Generate a local project status report:

```sh
make report
```

The report is written to `build/project-report.md` and summarizes the kernel
size budget, user ELF sizes, seeded GUI apps, minifs images, GUI smoke
screenshots, and serial logs.

Reset the persistent `/fs` area:

```sh
make image-reset-fs
```

## Verified

- `make` builds `build/buzzos.img`.
- `make smoke` boots QEMU, reaches the shell, verifies `/bin` and `/fs/apps`,
  checks `/proc`, verifies themed shell help and `/proc` read-only behavior,
  checks network status through `/proc/net` and `netstat`, serves deterministic
  host-side TCP HTTP responses, fetches one through `wget`, then exercises two
  concurrent BuzzOS stream sockets through `tcptwotest`, writes and reads a file
  under `/fs`, runs bad-ELF rejection fixtures, pipe and futex demos, checks
  pipe EOF/closed-end/blocking wakeups, verifies multi-stage pipelines and
  input/output/append redirection, verifies futex timeout, dead-waiter
  cleanup, and blocked waiter behavior through `/proc/threads` and
  `/proc/sync`, and checks process output.
- `make check-project` validates image layout, bootloader/Makefile sector
  agreement, user ELF load ranges, stack bounds, initrd hygiene, ELF loader
  hardening and bad runtime fixture coverage, kernel/user syscall ABI agreement,
  futex scheduler-backed blocking, TCP PCB ownership/demux buffers and smoke
  coverage, shell stdio-only fd inheritance, multi-stage pipeline/redirection
  support, pipe blocking semantics, initrd blob reachability, procfs/health
  diagnostic entries, seeded app manifests, and kernel placement outside the
  VGA/BIOS hole.
- `make app-check` validates seeded `/fs/apps` manifests and their generated
  user ELF files without running QEMU.
- `make fs-check` validates the minifs metadata in a raw disk image.
- `make fs-check-negative` mutates a disposable image in memory and verifies
  that the minifs checker rejects representative corruption cases.
- `make fs-check-repair` verifies that conservative minifs repair fixes stale
  free-inode metadata and bitmap drift while refusing unsafe corruptions.
- `fsstat` is covered by the serial smoke test and reports `/fs` usage from
  inside BuzzOS.
- `make verify` runs the serial smoke test, checks the smoke test image at
  `build/buzzos-test.img`, and runs negative and repair minifs corruption
  fixtures, covering post-boot formatting, app seeding, file creation,
  deletion, checker failure behavior, and safe host-side repair behavior.
- `make gui-smoke` boots a disposable image copy, drives user GUI examples, and
  validates generated screenshots under `build/gui-smoke`.
- `make report` writes a human-readable local status report to
  `build/project-report.md`, including kernel headroom, app inventory, minifs
  usage, screenshots, and logs.
- Visual QEMU screenshots verify:
  - `GUIDEMO` single-line textbox input and persistence.
  - `NOTES` multiline input and persistence.
  - `FORMS` multiple text boxes, cursor editing, live preview, and persistence.
  - `CALC` two text boxes, operation buttons, result feedback, and persistence.

## Lightweight Boundaries

- The GUI is still a full-screen user-space app manager, not a multi-window
  desktop.
- The filesystem is intentionally small and fixed-size.
- The network stack is useful for experiments, but not a mature production TCP/IP
  stack.
- The kernel remains freestanding and compact; richer UI behavior belongs in
  user-space apps unless a kernel primitive is truly needed.

## Next Maturity Targets

- Add stronger TCP regression coverage, retransmission, window tracking, and
  larger receive-flow behavior.
