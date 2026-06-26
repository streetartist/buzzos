#!/usr/bin/env python3
import argparse
import contextlib
import datetime as dt
import io
import re
import struct
from pathlib import Path

from check_minifs import FsError, MiniFsImage, parse_make_int

ROOT = Path(__file__).resolve().parents[1]


def read_text(path):
    return (ROOT / path).read_text(encoding="utf-8")


def read_text_if_exists(path):
    full = ROOT / path
    if not full.exists():
        return ""
    return full.read_text(encoding="utf-8")


def parse_make_words(text, name):
    m = re.search(rf"^{re.escape(name)}\s*:?=\s*(.*?)\s*$", text, re.M)
    return m.group(1).split() if m else []


def parse_manifest(text):
    result = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def rel(path):
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def bytes_text(value):
    if value is None:
        return "-"
    return f"{value} B ({value / 1024:.1f} KiB)"


def file_size(path):
    return path.stat().st_size if path.exists() else None


def elf_load_end(path):
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        return None
    _ident, _etype, _emachine, _version, _entry, phoff, _shoff, _flags, _ehsize, phentsize, phnum, *_rest = struct.unpack_from(
        "<16sHHIIIIIHHHHHH", data, 0
    )
    if phentsize != 32 or phoff + phnum * phentsize > len(data):
        return None
    highest = 0
    for i in range(phnum):
        off = phoff + i * phentsize
        ptype, _poff, vaddr, _paddr, _filesz, memsz, _flags, _align = struct.unpack_from("<IIIIIIII", data, off)
        if ptype == 1:
            highest = max(highest, vaddr + memsz)
    return highest or None


def collect_user_elves():
    user_dir = ROOT / "build" / "user"
    rows = []
    if not user_dir.exists():
        return rows
    for elf in sorted(user_dir.glob("*.elf")):
        data = elf.read_bytes()
        end = elf_load_end(elf)
        stripped = "-"
        if len(data) >= 52 and data[:4] == b"\x7fELF":
            shoff = struct.unpack_from("<I", data, 32)[0]
            shentsize = struct.unpack_from("<H", data, 46)[0]
            shnum = struct.unpack_from("<H", data, 48)[0]
            stripped = "yes" if shoff == 0 and shentsize == 0 and shnum == 0 else "no"
        rows.append({
            "name": elf.name,
            "size": elf.stat().st_size,
            "load_end": f"0x{end:06X}" if end else "-",
            "stripped": stripped,
        })
    return rows


def collect_initrd_status():
    initrd = ROOT / "src" / "kernel" / "initrd.h"
    mkinitrd = read_text_if_exists("tools/mkinitrd.py")
    bytes_per_line = "-"
    m = re.search(r"BYTES_PER_LINE\s*=\s*(\d+)", mkinitrd)
    if m:
        bytes_per_line = m.group(1)
    if not initrd.exists():
        return [{
            "file": rel(initrd),
            "status": "missing",
            "size": None,
            "lines": "-",
            "blobs": "-",
            "bytes_per_line": bytes_per_line,
        }]
    text = initrd.read_text(encoding="utf-8")
    return [{
        "file": rel(initrd),
        "status": "present",
        "size": initrd.stat().st_size,
        "lines": str(text.count("\n") + (0 if text.endswith("\n") else 1)),
        "blobs": str(len(re.findall(r"^#define\s+INITRD_[A-Z0-9_]+_SIZE\b", text, re.M))),
        "bytes_per_line": bytes_per_line,
    }]


def collect_apps():
    makefile = read_text_if_exists("Makefile")
    apps = parse_make_words(makefile, "GUI_APP_NAMES")
    rows = []
    for app in apps:
        manifest_path = ROOT / "src" / "user" / "bin" / f"{app}.app"
        manifest = parse_manifest(read_text_if_exists(rel(manifest_path))) if manifest_path.exists() else {}
        elf_path = ROOT / "build" / "user" / f"{app}.elf"
        rows.append({
            "app": app,
            "name": manifest.get("name", "-"),
            "version": manifest.get("version", "-"),
            "summary": manifest.get("summary", "-"),
            "state": manifest.get("state", "-"),
            "readme": manifest.get("readme", "-"),
            "elf_size": file_size(elf_path),
        })
    return rows


def collect_minifs(image_path, fs_start, fs_sectors):
    path = ROOT / image_path
    if not path.exists():
        return {"image": image_path, "status": "missing"}
    try:
        data = path.read_bytes()
        fs = MiniFsImage(data, fs_start, fs_sectors)
        if fs.is_empty_region():
            return {"image": image_path, "status": "empty"}
        with contextlib.redirect_stdout(io.StringIO()):
            fs.check()
        used_inodes = [inode for inode in fs.inodes if inode["used"]]
        dirs = [inode for inode in used_inodes if inode["type"] == 1]
        files = [inode for inode in used_inodes if inode["type"] == 2]
        used_blocks = sum(1 for value in fs.bitmap if value)
        return {
            "image": image_path,
            "status": "ok",
            "inodes": f"{len(used_inodes)}/{len(fs.inodes)}",
            "dirs": str(len(dirs)),
            "files": str(len(files)),
            "blocks": f"{used_blocks}/{fs.expected_blocks}",
            "paths": str(len(fs.paths)),
        }
    except FsError as exc:
        return {"image": image_path, "status": "fail", "error": str(exc)}


def collect_procfs_entries():
    source = read_text_if_exists("src/kernel/fs/procfs.c")
    rows = []
    for name in re.findall(r'\{\s*"([^"]+)",\s*PROC_NODE_[A-Z_]+\s*\}', source):
        rows.append({"file": f"/proc/{name}", "status": "registered"})
    return rows


def collect_project_identity():
    procfs = read_text_if_exists("src/kernel/fs/procfs.c")
    m = re.search(r"static\s+int\s+proc_about_text\(.*?\{(.*?)return\s+pos;", procfs, re.S)
    rows = []
    if m:
        for line in re.findall(r'append_text\(buf,\s*&pos,\s*cap,\s*"([^"]+)\\n"\);', m.group(1)):
            parts = line.split(" ", 1)
            if len(parts) == 2:
                rows.append({"item": parts[0], "value": parts[1]})
    if not rows:
        rows.append({"item": "/proc/about", "value": "missing"})
    return rows


def collect_health_interfaces():
    procfs = read_text_if_exists("src/kernel/fs/procfs.c")
    shell = read_text_if_exists("src/user/bin/shell.c")
    gui = read_text_if_exists("src/user/bin/gui.c")
    smoke = read_text_if_exists("scripts/smoke.ps1")
    return [
        {
            "interface": "/proc/health",
            "status": "yes" if '"health", PROC_NODE_HEALTH' in procfs and "proc_health_text" in procfs else "no",
            "evidence": "procfs",
        },
        {
            "interface": "shell health",
            "status": "yes" if 'cmd_cat("/proc/health")' in shell else "no",
            "evidence": "text shell",
        },
        {
            "interface": "GUI shell health",
            "status": "yes" if 'shell_cmd_cat("/proc/health")' in gui else "no",
            "evidence": "user GUI",
        },
        {
            "interface": "smoke coverage",
            "status": "yes" if "cat /proc/health" in smoke and "status\\s+ok" in smoke else "no",
            "evidence": "serial smoke",
        },
    ]


def collect_runtime_interfaces():
    procfs = read_text_if_exists("src/kernel/fs/procfs.c")
    m = re.search(r"static\s+int\s+proc_interfaces_text\(.*?\{(.*?)return\s+pos;", procfs, re.S)
    if not m:
        return []
    rows = []
    for line in re.findall(r'append_text\(buf,\s*&pos,\s*cap,\s*"([^"]+)\\n"\);', m.group(1)):
        if line.startswith("NAME "):
            continue
        parts = line.split(" ", 2)
        if len(parts) != 3:
            continue
        rows.append({
            "name": parts[0],
            "status": parts[1],
            "entrypoints": parts[2],
        })
    return rows


def collect_ipc_status():
    vfs_internal = read_text_if_exists("src/kernel/fs/vfs_internal.h")
    smoke = read_text_if_exists("scripts/smoke.ps1")
    pipe_buf = "-"
    m = re.search(r"^\s*#define\s+PIPE_BUFSZ\s+(\d+)", vfs_internal, re.M)
    if m:
        pipe_buf = m.group(1)
    checks = [
        ("pipetest", "pipe normal write/read"),
        ("pipeedgetest", "pipe eof/closed-reader edges"),
        ("pipeblocktest", "pipe reader/full-writer blocking"),
        ("futextest", "futex wait/wake"),
        ("futextimeouttest", "futex timeout/wake-before-timeout"),
        ("futexcanceltest", "futex dead-waiter cleanup"),
        ("futexblocktest", "futex blocked thread visibility"),
    ]
    rows = []
    for cmd, purpose in checks:
        rows.append({
            "check": cmd,
            "purpose": purpose,
            "smoke": "yes" if cmd in smoke else "no",
            "pipe_buf": pipe_buf if cmd.startswith("pipe") else "-",
        })
    return rows


def collect_screenshots():
    names = ["app-center", "forms-edit", "calc-edit", "notes-edit", "guidemo-edit"]
    rows = []
    for name in names:
        path = ROOT / "build" / "gui-smoke" / f"{name}.png"
        rows.append({
            "name": name,
            "path": rel(path),
            "size": file_size(path),
            "status": "present" if path.exists() else "missing",
        })
    return rows


def collect_logs():
    rows = []
    for log in ["build/serial-smoke.log", "build/serial-gui-smoke.log"]:
        path = ROOT / log
        rows.append({
            "path": log,
            "status": "present" if path.exists() else "missing",
            "size": file_size(path),
        })
    return rows


def table(headers, rows):
    out = []
    out.append("| " + " | ".join(headers) + " |")
    out.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        out.append("| " + " | ".join(str(row.get(header, "")) for header in headers) + " |")
    return out


def build_report():
    makefile = read_text_if_exists("Makefile")
    kernel_sectors = parse_make_int("KERNEL_SECTORS", 0)
    fs_start = parse_make_int("FS_START_SECTOR", 512)
    fs_sectors = parse_make_int("FS_SECTORS", 512)
    kernel_path = ROOT / "build" / "obj" / "kernel" / "kernel.bin"
    image_path = ROOT / "build" / "buzzos.img"
    kernel_size = file_size(kernel_path)
    kernel_limit = kernel_sectors * 512 if kernel_sectors else None
    kernel_headroom = kernel_limit - kernel_size if kernel_limit is not None and kernel_size is not None else None
    headroom_status = "ok"
    if kernel_headroom is None:
        headroom_status = "missing"
    elif kernel_headroom < 8192:
        headroom_status = "low"

    lines = []
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    lines.append("# BuzzOS Project Report")
    lines.append("")
    lines.append(f"Generated: {stamp}")
    lines.append("")

    lines.append("## Project Identity")
    lines.extend(table(["item", "value"], collect_project_identity()))
    lines.append("")

    lines.append("## Image")
    lines.extend(table(["item", "value"], [
        {"item": "image", "value": f"{rel(image_path)} ({bytes_text(file_size(image_path))})"},
        {"item": "kernel", "value": bytes_text(kernel_size)},
        {"item": "kernel limit", "value": bytes_text(kernel_limit)},
        {"item": "kernel headroom", "value": f"{bytes_text(kernel_headroom)} [{headroom_status}]"},
        {"item": "kernel sectors", "value": str(kernel_sectors)},
        {"item": "fs region", "value": f"LBA {fs_start}..{fs_start + fs_sectors - 1} ({fs_sectors} sectors)"},
    ]))
    lines.append("")

    user_elves = collect_user_elves()
    lines.append("## User ELFs")
    if user_elves:
        lines.extend(table(["name", "size", "load_end", "stripped"], [
            {"name": row["name"], "size": bytes_text(row["size"]), "load_end": row["load_end"], "stripped": row["stripped"]}
            for row in user_elves
        ]))
    else:
        lines.append("No user ELFs found. Run `make` first.")
    lines.append("")

    lines.append("## Initrd")
    lines.extend(table(["file", "status", "size", "lines", "blobs", "bytes_per_line"], [
        {
            "file": row["file"],
            "status": row["status"],
            "size": bytes_text(row["size"]),
            "lines": row["lines"],
            "blobs": row["blobs"],
            "bytes_per_line": row["bytes_per_line"],
        }
        for row in collect_initrd_status()
    ]))
    lines.append("")

    apps = collect_apps()
    lines.append("## GUI Apps")
    if apps:
        lines.extend(table(["app", "name", "version", "summary", "state", "readme", "elf_size"], [
            {
                "app": row["app"],
                "name": row["name"],
                "version": row["version"],
                "summary": row["summary"],
                "state": row["state"],
                "readme": row["readme"],
                "elf_size": bytes_text(row["elf_size"]),
            }
            for row in apps
        ]))
    else:
        lines.append("No GUI_APP_NAMES found in Makefile.")
    lines.append("")

    procfs = collect_procfs_entries()
    lines.append("## Procfs")
    if procfs:
        lines.extend(table(["file", "status"], procfs))
    else:
        lines.append("No procfs entries found.")
    lines.append("")

    lines.append("## Health Interfaces")
    lines.extend(table(["interface", "status", "evidence"], collect_health_interfaces()))
    lines.append("")

    runtime_interfaces = collect_runtime_interfaces()
    lines.append("## Runtime Interfaces")
    if runtime_interfaces:
        lines.extend(table(["name", "status", "entrypoints"], runtime_interfaces))
    else:
        lines.append("No `/proc/interfaces` matrix found.")
    lines.append("")

    lines.append("## IPC")
    lines.extend(table(["check", "purpose", "smoke", "pipe_buf"], collect_ipc_status()))
    lines.append("")

    lines.append("## Minifs")
    fs_rows = [collect_minifs("build/buzzos.img", fs_start, fs_sectors),
               collect_minifs("build/buzzos-test.img", fs_start, fs_sectors)]
    lines.extend(table(["image", "status", "inodes", "dirs", "files", "blocks", "paths", "error"], fs_rows))
    lines.append("")

    lines.append("## GUI Smoke Artifacts")
    lines.extend(table(["name", "status", "size", "path"], [
        {"name": row["name"], "status": row["status"], "size": bytes_text(row["size"]), "path": row["path"]}
        for row in collect_screenshots()
    ]))
    lines.append("")

    lines.append("## Logs")
    lines.extend(table(["path", "status", "size"], [
        {"path": row["path"], "status": row["status"], "size": bytes_text(row["size"])}
        for row in collect_logs()
    ]))
    lines.append("")

    lines.append("## Gates")
    lines.append("")
    lines.append("- `make verify` runs project checks, serial smoke with deterministic single/dual TCP socket coverage, minifs positive/negative/repair checks, and GUI smoke.")
    lines.append("- `make check-project` includes image, memory/VGA-hole, ELF loader hardening, initrd hygiene, syscall ABI, futex scheduler-backed blocking, TCP PCB/demux buffer/single-dual smoke coverage, procfs identity/health/interface diagnostics, shell stdio-only inheritance, multi-stage pipeline/redirection support, pipe blocking semantics, user ELF, initrd reachability, and app manifest checks.")
    lines.append("- `make fs-check-repair` verifies conservative minifs repair on disposable corrupted image copies.")
    lines.append("- `make report` writes this summary to `build/project-report.md`.")
    if headroom_status == "low":
        lines.append("- Kernel headroom is low; prefer user-space features or increase/reshape boot layout before adding kernel payload.")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate a BuzzOS local project report")
    parser.add_argument("--out", default="build/project-report.md", help="Markdown output path")
    parser.add_argument("--print", action="store_true", help="also print the report to stdout")
    args = parser.parse_args()

    report = build_report()
    out = ROOT / args.out if not Path(args.out).is_absolute() else Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(report + "\n", encoding="utf-8", newline="\n")
    if args.print:
        print(report)
    else:
        print(f"Wrote {rel(out)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
