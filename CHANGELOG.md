# Changelog

This file records the project-level evolution of BuzzOS. It is meant to be a
short log for reviewers and contributors; deeper design notes live under
`docs/`, and generated verification summaries can be produced with
`make report`.

## Unreleased

### Project Introduction

- Clarified BuzzOS as a small i386 POSIX-like operating system for learning
  and experiments, with a user shell, multitasking, syscalls, VFS, a persistent
  mini filesystem, TCP/UDP/ICMP networking, pipes, futex-style synchronization,
  and a user-space GUI app manager.
- Expanded the documentation map with project status, GUI app examples,
  procfs notes, minifs notes, IPC notes, and work-item tracking.
- Added local-first run and verification guidance for Windows/QEMU workflows,
  including visible QEMU runs that keep keyboard input inside the emulator.

### User Experience

- Added `/bin/echo` and `/bin/cat`, multi-stage shell pipelines, basic
  redirection, and stdio inheritance for spawned user programs.
- Added a user-space GUI app center backed by `/fs/apps`.
- Seeded GUI examples:
  - `guidemo`: buttons, swatches, a focused single-line textbox, mouse input,
    and saved state.
  - `notes`: a multiline text editor with persistent note storage.
  - `forms`: multiple text inputs with focus switching, cursor editing, live
    preview, and persisted form state.
- Added source-side app metadata and registry generation so GUI apps can ship
  with `.app` manifests, readmes, optional seed files, and generated kernel
  seed data.

### Kernel And Runtime

- Added `/proc` diagnostics for tasks, threads, memory, networking, sync
  waiters, file descriptors, and mounts.
- Added a multi-interface project identity surface through `/proc/about`, the
  text-shell `about` command, GUI-shell `about`, smoke coverage, and
  `make report` project identity reporting.
- Added a compact multi-interface health surface through `/proc/health`, the
  text-shell `health` command, GUI-shell `health`, smoke coverage, and
  `make report` interface reporting.
- Added a lightweight `/proc/interfaces` capability matrix with text-shell,
  GUI-shell, smoke, and report coverage for stable/experimental entrypoints.
- Added `/proc/limits`, text-shell `limits`, GUI-shell `limits`, smoke
  coverage, and `make report` runtime limit reporting for lightweight capacity
  discovery.
- Improved pipe behavior with blocking read/write wakeups and coverage for
  blocking pipe scenarios.
- Reworked futex wait/wake around scheduler-backed blocking, wake-by-address,
  timeout cleanup, and cancellation cleanup.
- Improved TCP sockets with per-socket PCB state, receive demux, buffering, and
  deterministic single/dual TCP smoke coverage.
- Hardened ELF loading with size-aware validation of ELF headers, program
  header ranges, segment sizes, user load ranges, and executable entry ranges.
- Tightened low-memory layout by moving the kernel load address and reserving
  kernel, stack, and user windows in the physical memory manager.

### Filesystem And Tooling

- Added `make help` / `tools/workflow.py` to print the recommended local
  workflow without building the image.
- Added `make doctor` / `tools/doctor.py` to preflight local Python, Make,
  PowerShell, NASM, LLVM, QEMU, and workspace paths before building or running.
- Added host-side minifs checks, negative checks, and repair checks.
- Added project consistency checks for image layout, stripped user ELF payloads,
  compact initrd rows, generated app registry data, and seeded app outputs.
- Added `make smoke`, `make gui-smoke`, `make verify`, and `make report`
  workflows as reviewer-friendly gates.

### Verification Log

- `make verify QEMU="C:\Program Files\qemu\qemu-system-i386.exe"`
- `make report`
- `python -m py_compile tools/check_project.py tools/project_report.py tools/mkinitrd.py`
- `make check-project`

The generated project report is written to `build/project-report.md`; the
`build/` directory remains a generated-artifact area.
