# BuzzOS

A minimal bootable i386 operating-system skeleton. The plan is to get the smallest loop working first, then layer on memory management, processes, ELF loading, filesystems, graphics, and a user-space — one stage at a time.

```text
BIOS -> boot sector -> protected mode -> C kernel -> VGA text output
```

Once it boots, QEMU shows:

```text
BuzzOS
minimal i686 kernel

next: memory, syscalls, ELF, VFS, framebuffer
```

> **This repo is a teaching project, not a finished one.**
> The long-term goal is a complete path from bare metal to a user-space that can run a C program compiled by `gcc`. `docs/tutorial.md` is extended stage by stage along that path; each new capability is reflected here in the checklist below.
> It is small right now. That is its current shape, not its destination.

Tutorial: [`docs/tutorial.md`](docs/tutorial.md) (Chinese, 14 chapters from toolchain to QEMU verification)
中文主版：[README.md](README.md)

---

## Done / planned

- [x] BIOS boot-sector signature check
- [x] Boot sector reads kernel from disk into `0x1000`
- [x] A20 gate enabled
- [x] GDT + 32-bit protected mode
- [x] freestanding C kernel entry
- [x] `.bss` zeroing
- [x] VGA text-mode output
- [ ] Serial (COM1) output
- [ ] IDT + interrupt handling
- [ ] Physical memory manager (E820 + bitmap / free list)
- [ ] Paging / virtual memory
- [ ] ELF loader
- [ ] Processes and scheduling
- [ ] System calls
- [ ] VFS (ramfs / devfs)
- [ ] Framebuffer graphics
- [ ] User space + loading user programs

Each item, once landed, gets a matching chapter in the tutorial. See [`docs/tutorial.md`](docs/tutorial.md) §13 for the recommended order.

---

## Build and run

Toolchain:

| Tool               | Purpose                                        | Required        |
| ------------------ | ---------------------------------------------- | --------------- |
| `nasm`             | Assembles the boot sector                      | Yes             |
| `clang`            | Compiles the freestanding C kernel             | Yes             |
| `ld.lld`           | Links the ELF                                  | Yes             |
| `llvm-objcopy`     | Strips the ELF into a raw binary kernel        | Yes             |
| `make`             | Drives the build pipeline                      | Yes             |
| `powershell`       | Stitches the image and cleans the build tree   | Windows only; on Unix use `dd` |
| `qemu-system-i386` | Boots the resulting image                      | Strongly recommended |

Build and run in one step:

```sh
make run
```

Build only, then boot manually:

```sh
make
qemu-system-i386 -drive format=raw,file=build/buzzos.img
```

The only artifact is `build/buzzos.img` (1 boot sector + 64 kernel sectors = 32 KiB + 512 B).

Toolchain verification, build-pipeline details, and troubleshooting live in [`docs/tutorial.md`](docs/tutorial.md) §1, §10, and §12.

---

## The repo is the tutorial

Four core files map to the four hops in the boot chain:

- Boot sector — [`src/boot/boot.asm`](src/boot/boot.asm)
- C kernel entry — [`src/kernel/kernel.c`](src/kernel/kernel.c)
- Linker script (load address, `.bss` layout) — [`linker.ld`](linker.ld)
- Image packing — [`tools/mkimage.ps1`](tools/mkimage.ps1)

Suggested reading: walk through [`docs/tutorial.md`](docs/tutorial.md) in order, cross-referencing each chapter to the corresponding file; after each chapter, `make run` to see the effect. §11 lists three small code changes (boot title, kernel text, screen color) that are the fastest way to verify the whole chain end to end.

---

## Contributing

The most useful contributions make the tutorial and the code grow together:

1. **Fix the tutorial.** Stale commands, drift between explanation and code, terminology that no longer matches — submit a PR against `docs/tutorial.md` directly.
2. **Add a chapter.** Pick an item from the §13 roadmap and write a chapter in the same style: how it works, what the code looks like, how to verify. Open an issue first to avoid collisions.
3. **Add a small demo.** Extend §11 with another "change one line, see the effect" example.

Issues are contributions too: getting stuck on a step, finding a step unclear, disagreeing with the roadmap — all of those directly shape the next chapter.

In code and prose, keep every section **short enough to read in one sitting** and every change **visible in QEMU on the next `make run`**. That's what keeps the repo usable as a working OS lab.
