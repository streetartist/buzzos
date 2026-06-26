#!/usr/bin/env python3
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_text(path):
    return (ROOT / path).read_text(encoding="utf-8")


def read_text_if_exists(path):
    full = ROOT / path
    if not full.exists():
        return ""
    return full.read_text(encoding="utf-8")


def fail(message):
    print(f"[fail] {message}", file=sys.stderr)
    raise SystemExit(1)


def ok(message):
    print(f"[ok] {message}")


def parse_make_int(text, name):
    m = re.search(rf"^{re.escape(name)}\s*:?=\s*(\d+)\s*$", text, re.M)
    if not m:
        fail(f"missing {name} in Makefile")
    return int(m.group(1))


def parse_make_words(text, name):
    m = re.search(rf"^{re.escape(name)}\s*:?=\s*(.*?)\s*$", text, re.M)
    if not m:
        fail(f"missing {name} in Makefile")
    return m.group(1).split()


def parse_define_int(text, name):
    m = re.search(rf"^\s*%define\s+{re.escape(name)}\s+(\d+)\b", text, re.M)
    if not m:
        fail(f"missing {name} in boot.asm")
    return int(m.group(1))


def parse_define_number(text, name):
    m = re.search(rf"^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|\d+)u?\b", text, re.M)
    if not m:
        fail(f"missing #define {name}")
    return int(m.group(1), 0)


def parse_c_int(text, name):
    m = re.search(rf"\b{re.escape(name)}\s*=\s*(\d+)\b", text)
    if not m:
        fail(f"missing {name}")
    return int(m.group(1))


def parse_hex_constant(text, name):
    m = re.search(rf"\b{re.escape(name)}\s*=\s*(0x[0-9A-Fa-f]+)", text)
    if not m:
        fail(f"missing {name}")
    return int(m.group(1), 16)


def parse_boot_stack_top(text):
    m = re.search(r"^\s*mov\s+esp,\s*(0x[0-9A-Fa-f]+|\d+)\b", text, re.M)
    if not m:
        fail("missing protected-mode boot stack setup")
    return int(m.group(1), 0)


def elf_section_range(path, wanted):
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        fail(f"{path.name}: unsupported ELF32 file")
    (_ident, _etype, _emachine, _version, _entry, _phoff, shoff, _flags,
     _ehsize, _phentsize, _phnum, shentsize, shnum, shstrndx) = struct.unpack_from(
        "<16sHHIIIIIHHHHHH", data, 0
    )
    if shentsize != 40 or shoff + shnum * shentsize > len(data) or shstrndx >= shnum:
        fail(f"{path.name}: invalid section headers")
    shstr_off = shoff + shstrndx * shentsize
    _name, _type, _flags, _addr, shstrtab_off, shstrtab_size, *_rest = struct.unpack_from("<IIIIIIIIII", data, shstr_off)
    if shstrtab_off + shstrtab_size > len(data):
        fail(f"{path.name}: invalid section-name table")
    names = data[shstrtab_off:shstrtab_off + shstrtab_size]

    def section_name(offset):
        if offset >= len(names):
            return ""
        end = names.find(b"\x00", offset)
        if end < 0:
            end = len(names)
        return names[offset:end].decode("ascii", errors="ignore")

    for i in range(shnum):
        off = shoff + i * shentsize
        name_off, _type, _flags, addr, _offset, size, *_rest = struct.unpack_from("<IIIIIIIIII", data, off)
        if section_name(name_off) == wanted:
            return addr, addr + size
    fail(f"{path.name}: missing section {wanted}")


def check_image_layout():
    makefile = read_text("Makefile")
    boot = read_text("src/boot/boot.asm")
    minifs_h = read_text("src/kernel/fs/minifs/minifs.h")
    kernel_sectors = parse_make_int(makefile, "KERNEL_SECTORS")
    boot_kernel_sectors = parse_define_int(boot, "KERNEL_SECTORS")
    fs_start = parse_make_int(makefile, "FS_START_SECTOR")
    fs_sectors = parse_make_int(makefile, "FS_SECTORS")
    minifs_start = parse_c_int(minifs_h, "MINIFS_LBA_START")
    minifs_sectors = parse_c_int(minifs_h, "MINIFS_SECTORS")

    if kernel_sectors != boot_kernel_sectors:
        fail(f"KERNEL_SECTORS mismatch: Makefile={kernel_sectors}, boot.asm={boot_kernel_sectors}")
    if fs_start != minifs_start:
        fail(f"FS_START_SECTOR mismatch: Makefile={fs_start}, minifs.h={minifs_start}")
    if fs_sectors != minifs_sectors:
        fail(f"FS_SECTORS mismatch: Makefile={fs_sectors}, minifs.h={minifs_sectors}")
    kernel_end = 1 + kernel_sectors
    if kernel_end > fs_start:
        fail(f"kernel area ends at LBA {kernel_end}, overlaps FS start LBA {fs_start}")

    kernel_bin = ROOT / "build/obj/kernel/kernel.bin"
    if not kernel_bin.exists():
        fail("missing build/obj/kernel/kernel.bin; run make first")
    kernel_size = kernel_bin.stat().st_size
    max_kernel = kernel_sectors * 512
    if kernel_size > max_kernel:
        fail(f"kernel.bin is {kernel_size} bytes, limit is {max_kernel}")

    image = ROOT / "build/buzzos.img"
    if image.exists():
        expected = (fs_start + fs_sectors) * 512
        actual = image.stat().st_size
        if actual != expected:
            fail(f"buzzos.img size is {actual}, expected {expected}")

    ok(f"image layout: kernel {kernel_size}/{max_kernel} bytes, fs LBA {fs_start}..{fs_start + fs_sectors - 1}")


def check_kernel_memory_layout():
    boot = read_text("src/boot/boot.asm")
    pmm_c = read_text("src/kernel/mm/pmm.c")
    kernel_elf = ROOT / "build/obj/kernel/kernel.elf"
    if not kernel_elf.exists():
        fail("missing build/obj/kernel/kernel.elf; run make first")
    text_start, _text_end = elf_section_range(kernel_elf, ".text")
    _bss_start, bss_end = elf_section_range(kernel_elf, ".bss")
    stack_top = parse_boot_stack_top(boot)
    managed_limit = parse_define_number(pmm_c, "PMM_MANAGED_LIMIT")
    stack_reserve = 0x10000
    vga_hole_start = 0xA0000
    vga_hole_end = 0x100000
    if text_start < vga_hole_end and bss_end > vga_hole_start:
        fail(f"kernel image 0x{text_start:X}..0x{bss_end:X} overlaps VGA/BIOS hole 0x{vga_hole_start:X}..0x{vga_hole_end:X}")
    if stack_top > managed_limit:
        fail(f"boot stack top 0x{stack_top:X} exceeds PMM managed limit 0x{managed_limit:X}")
    if bss_end > stack_top - stack_reserve:
        fail(f"kernel .bss ends at 0x{bss_end:X}, overlapping 64KiB boot stack below 0x{stack_top:X}")
    ok(f"kernel memory: image 0x{text_start:06X}..0x{bss_end:06X}, boot stack 0x{stack_top - stack_reserve:06X}..0x{stack_top:06X}")


def check_user_bounds():
    elf_c = read_text("src/kernel/core/elf.c")
    syscall_h = read_text("src/kernel/syscall/syscall_internal.h")
    paging_c = read_text("src/kernel/arch/i386/paging.c")
    user_h = read_text("src/kernel/arch/i386/user.h")

    load_start = parse_hex_constant(elf_c, "USER_LOAD_START")
    load_end = parse_hex_constant(elf_c, "USER_LOAD_END")
    ptr_start = parse_hex_constant(syscall_h, "USER_PTR_START")
    ptr_end = parse_hex_constant(syscall_h, "USER_PTR_END")
    space_start = parse_hex_constant(paging_c, "USER_SPACE_START")
    space_end = parse_hex_constant(paging_c, "USER_SPACE_END")
    stack_top = parse_hex_constant(user_h, "USER_DEFAULT_STACK_TOP")

    if not (load_start == ptr_start == space_start):
        fail("user start constants differ across ELF/syscall/paging")
    if not (load_start < load_end <= stack_top < ptr_end == space_end):
        fail("user load/stack/pointer bounds are inconsistent")
    if stack_top - load_end < 0x10000:
        fail("user stack is too close to the ELF load window")

    ok(f"user bounds: load 0x{load_start:06X}..0x{load_end:06X}, stack 0x{stack_top:06X}, mapped to 0x{ptr_end:06X}")
    return load_start, load_end, stack_top


def check_elf_loader_hardening():
    elf_c = read_text("src/kernel/core/elf.c")
    elf_h = read_text("src/kernel/core/elf.h")
    exec_c = read_text("src/kernel/core/exec.c")
    shell_c = read_text("src/user/bin/shell.c")
    smoke_ps1 = read_text("scripts/smoke.ps1")

    for snippet in [
        "uint32_t elf_load(const uint8_t *buf, size_t size)",
        "static int add_overflows_u32",
        "static int file_range_ok",
        "static int user_range_ok",
        "static int entry_in_segment",
        "ehdr->e_ehsize != sizeof(struct elf32_ehdr)",
        "ehdr->e_phentsize != sizeof(struct elf32_phdr)",
        "file_range_ok(ehdr->e_phoff, phdr_bytes, size)",
        "phdr->p_filesz > phdr->p_memsz",
        "file_range_ok(phdr->p_offset, phdr->p_filesz, size)",
        "user_range_ok(phdr->p_vaddr, phdr->p_memsz)",
        "entry_ok",
        "Validate all loadable segments before writing anything",
    ]:
        if snippet not in elf_c:
            fail(f"ELF loader hardening is missing: {snippet}")

    if "uint32_t elf_load(const uint8_t *buf, size_t size);" not in elf_h:
        fail("elf.h does not expose the size-aware elf_load signature")

    for snippet in [
        "elf_load(elf_data, elf_size)",
        "paging_destroy_user_space(proc_cr3)",
        'serial_puts("[exec] bad ELF\\n")',
    ]:
        if snippet not in exec_c:
            fail(f"exec bad-ELF path is missing: {snippet}")

    for snippet in [
        "cmd_elfbadtest",
        "make_bad_elf",
        "run_bad_elf_case",
        'run_bad_elf_case("vaddr"',
        'run_bad_elf_case("filesz"',
        'run_bad_elf_case("memsz"',
        'run_bad_elf_case("entry"',
        "spawn_process_args(path, argv, 1, SPAWN_FLAG_SILENT)",
    ]:
        if snippet not in shell_c:
            fail(f"shell is missing bad ELF runtime coverage: {snippet}")

    for snippet in [
        "elfbadtest",
        "elfbad: vaddr -1",
        "elfbad: filesz -1",
        "elfbad: memsz -1",
        "elfbad: entry -1",
        "elfbad: ok",
    ]:
        if snippet not in smoke_ps1:
            fail(f"smoke.ps1 is missing bad ELF coverage: {snippet}")

    ok("ELF loader: size-aware validation rejects malformed runtime exec fixtures")


def check_elf(path, load_start, load_end):
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        fail(f"{path.name}: not an ELF32 file")
    (ident, etype, emachine, version, entry, phoff, _shoff, _flags,
     ehsize, phentsize, phnum, _shentsize, _shnum, _shstrndx) = struct.unpack_from(
        "<16sHHIIIIIHHHHHH", data, 0
    )
    if ident[4] != 1 or ident[5] != 1 or etype != 2 or emachine != 3 or version != 1:
        fail(f"{path.name}: unsupported ELF header")
    if ehsize != 52 or phentsize != 32:
        fail(f"{path.name}: unexpected ELF/program-header size")
    if phoff + phnum * phentsize > len(data):
        fail(f"{path.name}: program headers outside file")

    saw_load = False
    entry_ok = False
    max_end = 0
    for i in range(phnum):
        off = phoff + i * phentsize
        ptype, poff, vaddr, _paddr, filesz, memsz, flags, _align = struct.unpack_from("<IIIIIIII", data, off)
        if ptype != 1:
            continue
        saw_load = True
        end = vaddr + memsz
        max_end = max(max_end, end)
        if filesz > memsz:
            fail(f"{path.name}: PT_LOAD filesz exceeds memsz")
        if poff + filesz > len(data):
            fail(f"{path.name}: PT_LOAD file range outside file")
        if vaddr < load_start or end > load_end:
            fail(f"{path.name}: PT_LOAD 0x{vaddr:X}..0x{end:X} outside load window")
        if flags & 1 and vaddr <= entry < end:
            entry_ok = True
    if not saw_load or not entry_ok:
        fail(f"{path.name}: missing valid loadable entry segment")
    return max_end


def check_user_elves(load_start, load_end, stack_top):
    user_dir = ROOT / "build/user"
    if not user_dir.exists():
        fail("missing build/user; run make first")
    elves = sorted(user_dir.glob("*.elf"))
    if not elves:
        fail("no user ELF files found")

    highest = 0
    highest_name = ""
    for elf in elves:
        end = check_elf(elf, load_start, load_end)
        if end > highest:
            highest = end
            highest_name = elf.name
    if highest >= stack_top:
        fail(f"user ELF {highest_name} reaches 0x{highest:X}, above stack 0x{stack_top:X}")
    ok(f"user ELF ranges: {len(elves)} files, highest {highest_name} ends at 0x{highest:06X}")


def parse_initrd_blobs(initrd):
    pairs = {}
    for macro, data in re.findall(
        r"#define\s+(INITRD_[A-Z0-9_]+_SIZE)\s+\d+\s+"
        r"static\s+const\s+uint8_t\s+(initrd_[A-Za-z0-9_]+_data)\[",
        initrd,
    ):
        if data in pairs:
            fail(f"duplicate initrd data symbol: {data}")
        pairs[data] = macro
    if not pairs:
        fail("no initrd blobs found in src/kernel/initrd.h")
    return pairs


def check_initrd_reachability():
    initrd = read_text("src/kernel/initrd.h")
    kernel_c = read_text("src/kernel/core/kernel.c")
    app_registry = read_text_if_exists("src/kernel/app_registry.h")
    refs = kernel_c + "\n" + app_registry
    blobs = parse_initrd_blobs(initrd)

    missing_data = []
    missing_size = []
    for data, macro in sorted(blobs.items()):
        if not re.search(rf"\b{re.escape(data)}\b", refs):
            missing_data.append(data)
        if not re.search(rf"\b{re.escape(macro)}\b", refs):
            missing_size.append(macro)
    if missing_data:
        fail("initrd blobs generated but not reachable from kernel/app registry: " + ", ".join(missing_data))
    if missing_size:
        fail("initrd size macros generated but not reachable from kernel/app registry: " + ", ".join(missing_size))

    ramfs_paths = []
    for path, data, macro in re.findall(
        r'ramfs_register\(\s*"([^"]+)"\s*,\s*(initrd_[A-Za-z0-9_]+_data)\s*,\s*(INITRD_[A-Z0-9_]+_SIZE)\s*\)',
        kernel_c,
    ):
        if data not in blobs:
            fail(f"ramfs_register({path}) uses unknown initrd blob {data}")
        if blobs[data] != macro:
            fail(f"ramfs_register({path}) uses {macro}, expected {blobs[data]} for {data}")
        ramfs_paths.append(path)
    if not ramfs_paths:
        fail("kernel does not register any initrd blobs into ramfs")
    if "/bin/sh" not in ramfs_paths:
        fail("kernel does not register /bin/sh from initrd")

    ok(f"initrd reachability: {len(blobs)} blobs, {len(ramfs_paths)} ramfs entries")


def check_initrd_hygiene():
    makefile = read_text("Makefile")
    mkinitrd = read_text("tools/mkinitrd.py")
    user_dir = ROOT / "build/user"
    if not user_dir.exists():
        fail("missing build/user; run make first")

    if "$(OBJCOPY) --strip-sections $@" not in makefile:
        fail("Makefile does not strip user ELF section metadata before initrd embedding")
    if "BYTES_PER_LINE = 32" not in mkinitrd:
        fail("mkinitrd.py should emit compact 32-byte rows to reduce generated diff noise")

    noisy = []
    for elf in sorted(user_dir.glob("*.elf")):
        data = elf.read_bytes()
        if len(data) < 52 or data[:4] != b"\x7fELF":
            continue
        shoff = struct.unpack_from("<I", data, 32)[0]
        shentsize = struct.unpack_from("<H", data, 46)[0]
        shnum = struct.unpack_from("<H", data, 48)[0]
        if shoff != 0 or shentsize != 0 or shnum != 0:
            noisy.append(f"{elf.name}: shoff={shoff} shentsize={shentsize} shnum={shnum}")
        for marker in [b".comment", b"clang version", b"LLVM", b".symtab", b".strtab"]:
            if marker in data:
                noisy.append(f"{elf.name}: contains {marker.decode('ascii', errors='ignore')}")
                break
    if noisy:
        fail("user ELF initrd payloads are not stripped: " + "; ".join(noisy))

    ok("initrd hygiene: user ELF payloads are section-stripped and mkinitrd emits compact rows")


def parse_syscall_enums(text, label):
    result = {}
    for block in re.findall(r"\benum\s*(?:[A-Za-z_][A-Za-z0-9_]*)?\s*\{(.*?)\}\s*;", text, re.S):
        value = -1
        for raw in block.split(","):
            entry = raw.strip()
            if not entry:
                continue
            m = re.match(r"(SYS_[A-Z0-9_]+)(?:\s*=\s*(0x[0-9A-Fa-f]+|\d+))?$", entry)
            if not m:
                continue
            name, explicit = m.groups()
            if explicit is not None:
                value = int(explicit, 0)
            else:
                value += 1
            if name in result and result[name] != value:
                fail(f"{label}: duplicate {name} values {result[name]} and {value}")
            result[name] = value
    if not result:
        fail(f"{label}: no SYS_* enum values found")
    return result


def check_syscall_abi():
    kernel_h = read_text("src/kernel/syscall/syscall.h")
    kernel_c = read_text("src/kernel/syscall/syscall.c")
    libc_c = read_text("src/user/libc/libc.c")

    kernel = parse_syscall_enums(kernel_h, "kernel syscall.h")
    user = parse_syscall_enums(libc_c, "user libc.c")

    missing_in_user = sorted(set(kernel) - set(user))
    extra_in_user = sorted(set(user) - set(kernel))
    if missing_in_user:
        fail("user libc.c is missing syscall numbers: " + ", ".join(missing_in_user))
    if extra_in_user:
        fail("user libc.c has syscall numbers not in kernel: " + ", ".join(extra_in_user))

    mismatched = []
    for name in sorted(kernel):
        if kernel[name] != user[name]:
            mismatched.append(f"{name} kernel={kernel[name]} user={user[name]}")
    if mismatched:
        fail("syscall number mismatch: " + "; ".join(mismatched))

    table_entries = set(re.findall(r"syscall_table\[\s*(SYS_[A-Z0-9_]+)\s*\]\s*=", kernel_c))
    missing_table = sorted(set(kernel) - table_entries)
    unknown_table = sorted(table_entries - set(kernel))
    if missing_table:
        fail("kernel syscall_table missing entries: " + ", ".join(missing_table))
    if unknown_table:
        fail("kernel syscall_table references unknown syscalls: " + ", ".join(unknown_table))

    used_by_libc = set(re.findall(r"\b(SYS_[A-Z0-9_]+)\b", libc_c))
    missing_wrappers = sorted(set(kernel) - used_by_libc)
    if missing_wrappers:
        fail("user libc.c does not reference syscalls: " + ", ".join(missing_wrappers))

    ok(f"syscall ABI: {len(kernel)} numbers match user libc and kernel table")


def check_network_socket_state():
    sys_net = read_text("src/kernel/syscall/sys_net.c")
    net_c = read_text("src/kernel/net/net.c")
    net_h = read_text("src/kernel/net/net.h")
    shell_c = read_text("src/user/bin/shell.c")
    smoke_ps1 = read_text("scripts/smoke.ps1")

    if "active_tcp_socket" in sys_net:
        fail("sys_net.c still gates stream sockets through active_tcp_socket")

    m = re.search(r"struct\s+socket_entry\s*\{(.*?)\};", sys_net, re.S)
    if not m:
        fail("sys_net.c: missing struct socket_entry")
    if "struct net_tcp_pcb tcp;" not in m.group(1):
        fail("socket_entry does not carry a per-socket TCP PCB")
    if sys_net.count("struct net_tcp_pcb tcp;") != 1:
        fail("sys_net.c should not copy registered TCP PCBs into local structs")
    if "s->tcp =" in sys_net:
        fail("sys_net.c should not assign/copy registered TCP PCB structs")

    if re.search(r"\bnet_tcp_(connect|send|recv|close)\s*\(", sys_net):
        fail("sys_net.c stream path should use net_tcp_*_pcb APIs")

    required = [
        "net_tcp_connect_pcb",
        "net_tcp_send_pcb",
        "net_tcp_recv_pcb",
        "net_tcp_close_pcb",
    ]
    for name in required:
        if name not in sys_net:
            fail(f"sys_net.c does not reference {name}")

    if "static struct net_tcp_pcb legacy_tcp;" not in net_c:
        fail("net.c should keep legacy TCP wrappers on top of a PCB")
    if re.search(r"static\s+struct\s*\{.*?\}\s*tcp\s*;", net_c, re.S):
        fail("net.c still defines the old global anonymous TCP state")

    for snippet in [
        "NET_TCP_RX_CAP",
        "registered",
        "rx_len",
        "rx_buf[NET_TCP_RX_CAP]",
        "struct net_tcp_pcb *next",
    ]:
        if snippet not in net_h:
            fail(f"net_tcp_pcb is missing {snippet}")

    for snippet in [
        "dev_recv_raw",
        "net_tcp_dispatch_frame",
        "net_tcp_register_pcb",
        "net_tcp_unregister_pcb",
        "net_tcp_queue_rx",
        "net_tcp_take_rx",
        "net_tcp_rx_buffered",
        "net_tcp_rx_dropped",
        "net_tcp_dispatch_frame(rxbuf, n)",
    ]:
        if snippet not in net_c:
            fail(f"net.c is missing TCP demux/buffer primitive: {snippet}")

    for snippet in [
        "wget <host> [port]",
        "tcptwotest <host> <p1> <p2>",
        "cmd_tcptwotest",
        "parse_ipv4(host, &ip)",
        "addr.sin_port = htons((uint16_t)port)",
    ]:
        if snippet not in shell_c:
            fail(f"shell wget command is missing TCP socket test support: {snippet}")

    for snippet in [
        "Start-TcpSmokeServer",
        "Wait-TcpSmokeServer",
        "BUZZOS_TCP_SMOKE_OK",
        "BUZZOS_TCP_TWO_A",
        "BUZZOS_TCP_TWO_B",
        "tcptwotest 10.0.2.2",
        "wget 10.0.2.2",
    ]:
        if snippet not in smoke_ps1:
            fail(f"smoke.ps1 is missing deterministic TCP socket coverage: {snippet}")

    ok("network sockets: stream sockets use per-socket TCP PCBs with rx demux buffers and single/dual smoke TCP coverage")


def check_procfs_diagnostics():
    procfs = read_text("src/kernel/fs/procfs.c")
    shell_c = read_text("src/user/bin/shell.c")
    smoke_ps1 = read_text("scripts/smoke.ps1")

    for entry in ["tasks", "threads", "meminfo", "net", "sync", "fds", "mounts"]:
        if f'{{ "{entry}",' not in procfs:
            fail(f"procfs is missing /proc/{entry}")

    for snippet in [
        "PROC_NODE_FDS",
        "proc_fds_text",
        "OWNER FD OF REFS FLAGS KIND NAME DETAIL",
        "fd_to_open_file(owner, fd)",
        "pipe=",
    ]:
        if snippet not in procfs:
            fail(f"procfs fd diagnostics missing: {snippet}")

    if "cmd_fdstat" not in shell_c or 'cmd_cat("/proc/fds")' not in shell_c:
        fail("shell is missing fdstat /proc/fds command")
    for snippet in ["cat /proc/fds", "fdstat", "OWNER\\s+FD\\s+OF\\s+REFS"]:
        if snippet not in smoke_ps1:
            fail(f"smoke.ps1 is missing /proc/fds coverage: {snippet}")

    ok("procfs diagnostics: /proc/fds and fdstat are covered")


def check_futex_blocking():
    sys_ipc = read_text("src/kernel/syscall/sys_ipc.c")
    sys_ipc_h = read_text("src/kernel/syscall/sys_ipc.h")
    task_c = read_text("src/kernel/sched/task.c")
    task_h = read_text("src/kernel/sched/task.h")
    procfs = read_text("src/kernel/fs/procfs.c")
    shell_c = read_text("src/user/bin/shell.c")
    smoke_ps1 = read_text("scripts/smoke.ps1")

    for snippet in [
        "struct futex_waiter",
        "futex_waiters[MAX_FUTEX_WAITERS]",
        "task_block_current();",
        "task_block_current_until(deadline);",
        "task_wake(futex_waiters[i].task_id)",
        "futex_cancel_task_locked",
        "futex_status_text",
        "SLOT TID ADDR       WOKEN",
    ]:
        if snippet not in sys_ipc:
            fail(f"futex scheduler-backed wait support is missing: {snippet}")

    if "void futex_cancel_task_locked(int task_id);" not in sys_ipc_h:
        fail("sys_ipc.h does not expose futex_cancel_task_locked")

    for snippet in [
        "futex_cancel_task_locked(current_task->id)",
        "current_task->state = TASK_BLOCKED",
        "void task_block_current(void)",
        "void task_block_current_until(uint32_t wake_tick)",
        "int task_wake(int id)",
    ]:
        if snippet not in task_c + "\n" + task_h:
            fail(f"scheduler futex integration is missing: {snippet}")

    for snippet in [
        "PROC_NODE_SYNC",
        "futex_status_text",
        "TASK_BLOCKED",
        "BLOCKED",
    ]:
        if snippet not in procfs + "\n" + task_c:
            fail(f"proc/scheduler diagnostics are missing futex blocking visibility: {snippet}")

    for snippet in [
        "cmd_futexblocktest",
        "futex_wait(&futex_block_word, 0)",
        "cmd_threads()",
        "cmd_syncstat()",
        "futexblock: waiting threads",
    ]:
        if snippet not in shell_c:
            fail(f"shell is missing futex blocking coverage: {snippet}")

    for snippet in [
        "futexblocktest",
        "futexblock: waiting threads",
        "BLOCKED\\s+tty\\s+user_thread",
        "futex_waiters\\s+1/32",
        "futexblock: woke\\s+1",
        "futexcancel: killed 34 wait -2",
    ]:
        if snippet not in smoke_ps1:
            fail(f"smoke.ps1 is missing futex blocking coverage: {snippet}")

    ok("futex: waits block in the scheduler, wake by address, and smoke covers blocked visibility/cancel cleanup")


def check_shell_pipeline():
    vfs_h = read_text("src/kernel/fs/vfs.h")
    vfs_c = read_text("src/kernel/fs/vfs.c")
    exec_c = read_text("src/kernel/core/exec.c")
    sys_proc = read_text("src/kernel/syscall/sys_proc.c")
    libc_h = read_text("src/user/libc/libc.h")
    shell_c = read_text("src/user/bin/shell.c")
    makefile = read_text("Makefile")
    kernel_c = read_text("src/kernel/core/kernel.c")
    smoke_ps1 = read_text("scripts/smoke.ps1")

    for path in ["src/user/bin/cat.c", "src/user/bin/echo.c"]:
        if not (ROOT / path).exists():
            fail(f"missing pipeline user program: {path}")

    for snippet in [
        "int  vfs_clone_fd_table(int dst_task_id, int src_task_id)",
        "int vfs_clone_fd_table(int dst_task_id, int src_task_id)",
        "int  vfs_clone_stdio(int dst_task_id, int src_task_id)",
        "int vfs_clone_stdio(int dst_task_id, int src_task_id)",
        "clone_fd_range(dst_task_id, src_task_id, 3)",
        "p->readers++",
        "p->writers++",
        "src->vnode.data == &src->stream",
    ]:
        source = vfs_h + "\n" + vfs_c
        if snippet not in source:
            fail(f"VFS fd inheritance is missing: {snippet}")

    for snippet in [
        "exec_start_args_with_fds",
        "vfs_clone_fd_table(id, inherit_fd_owner)",
        "vfs_clone_stdio(id, inherit_fd_owner)",
        "inherit_fd_owner >= 0",
        "inherit_stdio_only",
    ]:
        if snippet not in exec_c:
            fail(f"exec fd inheritance path is missing: {snippet}")

    for snippet in [
        "flags & 2u",
        "flags & 4u",
        "current_fd_owner()",
        "exec_start_args_with_fds",
        "inherit_stdio && !inherit_all",
    ]:
        if snippet not in sys_proc:
            fail(f"spawn fd inheritance flag is missing: {snippet}")

    if "SPAWN_FLAG_INHERIT_FDS 2" not in libc_h:
        fail("user libc is missing SPAWN_FLAG_INHERIT_FDS")
    if "SPAWN_FLAG_INHERIT_STDIO 4" not in libc_h:
        fail("user libc is missing SPAWN_FLAG_INHERIT_STDIO")

    for snippet in [
        "execute_pipeline",
        "line[i] == '|'",
        "PIPELINE_MAX 6",
        "struct pipeline_cmd",
        "parse_pipeline_cmd",
        "has_exec_operator",
        "pipe(fds)",
        "cmds[i].append ? O_APPEND : O_TRUNC",
        "SPAWN_FLAG_INHERIT_STDIO",
        "echo hello | cat",
        "cat < /fs/out",
    ]:
        if snippet not in shell_c:
            fail(f"shell pipeline support is missing: {snippet}")

    for snippet in [
        "CAT_ELF",
        "ECHO_ELF",
        "/bin/cat $(CAT_ELF)",
        "/bin/echo $(ECHO_ELF)",
        "src/user/bin/cat.c",
        "src/user/bin/echo.c",
    ]:
        if snippet not in makefile:
            fail(f"Makefile is missing pipeline user program wiring: {snippet}")

    for snippet in [
        'ramfs_register("/bin/cat"',
        'ramfs_register("/bin/echo"',
    ]:
        if snippet not in kernel_c:
            fail(f"kernel initrd registration is missing: {snippet}")

    for snippet in [
        "help pipes",
        '"|" { return "shift-backslash" }',
        '">" { return "shift-dot" }',
        '"<" { return "shift-comma" }',
        "echo pipelinesmokeok | cat",
        "echo multipipesmokeok | cat | cat",
        "echo redir-one > /fs/redir",
        "echo redir-two >> /fs/redir",
        "cat < /fs/redir",
        "pipelinesmokeok",
        "multipipesmokeok",
        "\\[pipe\\] exited 0 \\| 0",
        "\\[pipe\\] exited 0 \\| 0 \\| 0",
        "redir-one",
        "redir-two",
    ]:
        if snippet not in smoke_ps1:
            fail(f"smoke.ps1 is missing shell pipeline coverage: {snippet}")

    ok("shell pipeline: stdio-only inheritance supports multi-stage pipes and redirection in smoke")


def check_pipe_blocking():
    vfs_internal = read_text("src/kernel/fs/vfs_internal.h")
    pipefs_c = read_text("src/kernel/fs/pipefs.c")
    task_h = read_text("src/kernel/sched/task.h")
    task_c = read_text("src/kernel/sched/task.c")
    shell_c = read_text("src/user/bin/shell.c")
    smoke_ps1 = read_text("scripts/smoke.ps1")

    for snippet in [
        "uint32_t read_waiters;",
        "uint32_t write_waiters;",
    ]:
        if snippet not in vfs_internal:
            fail(f"pipe object is missing waiter state: {snippet}")

    for snippet in [
        "void task_prepare_block_current(uint32_t wake_tick)",
        "current_task->state = TASK_BLOCKED",
        "task_prepare_block_current(0)",
        "task_prepare_block_current(wake_tick ? wake_tick : 1)",
    ]:
        if snippet not in task_h + "\n" + task_c:
            fail(f"scheduler is missing reusable task blocking primitive: {snippet}")

    for snippet in [
        "static int pipe_wait(struct pipe_obj *p, int end)",
        "static void pipe_wake_mask(uint32_t *mask)",
        "task_prepare_block_current(0)",
        "vfs_unlock();",
        "task_yield();",
        "vfs_lock();",
        "p->read_waiters |= bit",
        "p->write_waiters |= bit",
        "pipe_wake_mask(&p->write_waiters)",
        "pipe_wake_mask(&p->read_waiters)",
        "while (p->count == 0 && p->writers > 0)",
        "while (p->count == PIPE_BUFSZ && p->readers > 0)",
    ]:
        if snippet not in pipefs_c:
            fail(f"pipefs blocking/wake support is missing: {snippet}")

    if re.search(r"if\s*\(\s*p->count\s*==\s*0\s*&&\s*p->writers\s*>\s*0\s*\)\s*return\s+-1\s*;", pipefs_c):
        fail("pipe_read still returns -1 for an empty pipe with live writers")
    if re.search(r"if\s*\(\s*p->count\s*==\s*PIPE_BUFSZ\s*&&\s*p->readers\s*>\s*0\s*\)\s*return\s+-1\s*;", pipefs_c):
        fail("pipe_write still returns -1 for a full pipe with live readers")

    for snippet in [
        "cmd_pipeblocktest",
        "pipe_block_reader_thread",
        "pipe_block_writer_thread",
        "pipeblock: reader %d write %d %s",
        "pipeblock: writer %d drain %d+%d",
    ]:
        if snippet not in shell_c:
            fail(f"shell is missing pipe blocking coverage: {snippet}")

    for snippet in [
        "pipeblocktest",
        "pipeblock: reader 4 write 4 wake",
        "pipeblock: writer 600 drain 512\\+88",
    ]:
        if snippet not in smoke_ps1:
            fail(f"smoke.ps1 is missing pipe blocking coverage: {snippet}")

    ok("pipes: reads/writes block on empty/full buffers and smoke covers wakeups")


def decode_c_strings(block):
    parts = re.findall(r'"((?:\\.|[^"\\])*)"', block)
    out = []
    for part in parts:
        out.append(bytes(part, "utf-8").decode("unicode_escape"))
    return "".join(out)


def parse_manifest_block(source, symbol):
    m = re.search(
        rf"static\s+const\s+char\s+{re.escape(symbol)}_manifest\[\]\s*=\s*(.*?);",
        source,
        re.S,
    )
    if not m:
        fail(f"missing {symbol}_manifest in app registry sources")
    text = decode_c_strings(m.group(1))
    result = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def parse_manifest_text(text):
    result = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def discover_app_manifests(source):
    apps = []
    seen = set()
    for m in re.finditer(
        r"static\s+const\s+char\s+([A-Za-z_][A-Za-z0-9_]*)_manifest\[\]\s*=",
        source,
    ):
        symbol = m.group(1)
        manifest = parse_manifest_block(source, symbol)
        exec_path = manifest.get("exec", "")
        if not exec_path.startswith("/fs/apps/"):
            continue
        app = exec_path.rsplit("/", 1)[-1]
        if app in seen:
            fail(f"duplicate app manifest for {app}")
        seen.add(app)
        apps.append((app, symbol, manifest))
    if not apps:
        fail("no /fs/apps manifests found in app registry sources")
    return apps


def check_app_manifests(list_only=False):
    kernel_c = read_text("src/kernel/core/kernel.c")
    app_registry = read_text_if_exists("src/kernel/app_registry.h")
    source = kernel_c + "\n" + app_registry
    makefile = read_text("Makefile")
    app_names = set(parse_make_words(makefile, "GUI_APP_NAMES"))
    apps = discover_app_manifests(source)
    discovered = {app for app, _symbol, _manifest in apps}

    missing_from_registry = sorted(app_names - discovered)
    extra_in_registry = sorted(discovered - app_names)
    if missing_from_registry:
        fail("GUI_APP_NAMES missing from generated app registry: " + ", ".join(missing_from_registry))
    if extra_in_registry:
        fail("generated app registry has apps outside GUI_APP_NAMES: " + ", ".join(extra_in_registry))

    if list_only:
        print("app       version  kind      exec")
        print("--------  -------  --------  ----------------")
        for app, _symbol, manifest in apps:
            print(f"{app:<8}  {manifest.get('version', '-'):<7}  {manifest.get('kind', '-'):<8}  {manifest.get('exec', '-')}")
        return

    required = ["name", "kind", "version", "summary", "exec", "state", "source"]
    seeded_manifest_paths = set(re.findall(r'"/fs/apps/([A-Za-z0-9_]+)\.app"', source))
    seeded_exec_paths = set(re.findall(r'"/fs/apps/([A-Za-z0-9_]+)"', source))

    for app, symbol, manifest in apps:
        missing = [key for key in required if key not in manifest or not manifest[key]]
        if missing:
            fail(f"{app}.app manifest missing keys: {', '.join(missing)}")
        if symbol not in (app, f"app_{app}"):
            fail(f"{app}.app manifest symbol should be {app}_manifest, got {symbol}_manifest")
        if manifest["exec"] != f"/fs/apps/{app}":
            fail(f"{app}.app exec should be /fs/apps/{app}")
        if not manifest["state"].startswith("/fs/apps/"):
            fail(f"{app}.app state should live under /fs/apps")
        if app not in app_names:
            fail(f"{app} is missing from GUI_APP_NAMES in Makefile")
        source = ROOT / manifest["source"]
        if not source.exists():
            fail(f"{app}.app source does not exist: {manifest['source']}")
        if not (ROOT / f"build/user/{app}.elf").exists():
            fail(f"missing build/user/{app}.elf")
        if app not in seeded_manifest_paths:
            fail(f"kernel does not seed /fs/apps/{app}.app")
        if app not in seeded_exec_paths:
            fail(f"kernel does not seed /fs/apps/{app}")
        sidecar = ROOT / f"src/user/bin/{app}.app"
        if not sidecar.exists():
            fail(f"missing source manifest sidecar: src/user/bin/{app}.app")
        sidecar_manifest = parse_manifest_text(sidecar.read_text(encoding="utf-8"))
        for key in required:
            if sidecar_manifest.get(key) != manifest.get(key):
                fail(f"{app}.app generated registry is out of sync for key {key}")
    ok("app manifests: " + ", ".join(app for app, _symbol, _manifest in apps))


def main():
    parser = argparse.ArgumentParser(description="BuzzOS host-side consistency checks")
    parser.add_argument("--apps-only", action="store_true", help="only validate /fs/apps manifests and build outputs")
    parser.add_argument("--list-apps", action="store_true", help="list kernel-seeded /fs/apps manifests")
    args = parser.parse_args()

    if args.list_apps:
        check_app_manifests(list_only=True)
        return
    if args.apps_only:
        check_app_manifests()
        print("App check passed.")
        return

    check_image_layout()
    check_kernel_memory_layout()
    load_start, load_end, stack_top = check_user_bounds()
    check_elf_loader_hardening()
    check_syscall_abi()
    check_network_socket_state()
    check_procfs_diagnostics()
    check_futex_blocking()
    check_shell_pipeline()
    check_pipe_blocking()
    check_user_elves(load_start, load_end, stack_top)
    check_initrd_reachability()
    check_initrd_hygiene()
    check_app_manifests()
    print("Project check passed.")


if __name__ == "__main__":
    main()
