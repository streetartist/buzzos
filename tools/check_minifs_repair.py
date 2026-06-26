#!/usr/bin/env python3
import argparse
import contextlib
import io
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from check_minifs import (
    FsError,
    MiniFsImage,
    MINIFS_INODES,
    ROOT,
    SECTOR_SIZE,
    parse_make_int,
)


def fail(message):
    raise SystemExit(f"[fail] {message}")


def ok(message):
    print(f"[ok] {message}")


def check_bytes(image, fs_start, fs_sectors):
    fs = MiniFsImage(bytes(image), fs_start, fs_sectors)
    with contextlib.redirect_stdout(io.StringIO()):
        if fs.is_empty_region():
            raise FsError("image has an unformatted all-zero /fs region")
        fs.check()
    return fs


def expect_check_failure(name, image, fs_start, fs_sectors, expected):
    try:
        check_bytes(image, fs_start, fs_sectors)
    except FsError as exc:
        message = str(exc)
        if not re.search(expected, message):
            fail(f"{name}: expected /{expected}/, got: {message}")
        return message
    fail(f"{name}: corrupted image unexpectedly passed before repair")


def run_checker(args, expect_success=True):
    cmd = [sys.executable, str(ROOT / "tools" / "check_minifs.py"), *args]
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    output = (proc.stdout + proc.stderr).strip()
    if expect_success and proc.returncode != 0:
        fail(f"checker failed: {' '.join(cmd)}\n{output}")
    if not expect_success and proc.returncode == 0:
        fail(f"checker unexpectedly succeeded: {' '.join(cmd)}\n{output}")
    return output


def put_u16(image, offset, value):
    struct.pack_into("<H", image, offset, value)


def put_u32(image, offset, value):
    struct.pack_into("<I", image, offset, value)


def inode_offset(fs_start, ino):
    return (fs_start + 1 + ino) * SECTOR_SIZE


def bitmap_offset(fs_start):
    return (fs_start + 1 + MINIFS_INODES) * SECTOR_SIZE


def find_used_block(fs):
    for index, value in enumerate(fs.bitmap):
        if value:
            return index
    fail("baseline image has no used data blocks")


def find_free_block(fs):
    for index, value in enumerate(fs.bitmap):
        if not value:
            return index
    fail("baseline image has no free data blocks")


def find_free_inode(fs):
    for inode in fs.inodes:
        if not inode["used"]:
            return inode["ino"]
    fail("baseline image has no free inodes")


def find_inode_with_direct_block(fs):
    for inode in fs.inodes:
        if not inode["used"]:
            continue
        for logical, raw in enumerate(inode["blocks"]):
            if raw:
                return inode["ino"], logical, raw
    fail("baseline image has no direct block references")


def find_inode_with_direct_block_and_free_slot(fs):
    for inode in fs.inodes:
        if not inode["used"]:
            continue
        used_slot = None
        free_slot = None
        for logical, raw in enumerate(inode["blocks"]):
            if raw and used_slot is None:
                used_slot = (logical, raw)
            if raw == 0 and free_slot is None:
                free_slot = logical
        if used_slot is not None and free_slot is not None:
            return inode["ino"], used_slot[0], used_slot[1], free_slot
    fail("baseline image has no direct block reference with a free direct slot")


def repair_cases(baseline, fs_start):
    used_block = find_used_block(baseline)
    free_block = find_free_block(baseline)
    free_inode = find_free_inode(baseline)
    bitmap_off = bitmap_offset(fs_start)

    def stale_free_inode(image):
        put_u32(image, inode_offset(fs_start, free_inode) + 4, 1)

    def missing_bitmap_mark(image):
        image[bitmap_off + used_block] = 0

    def leaked_bitmap_mark(image):
        image[bitmap_off + free_block] = 1

    def invalid_bitmap_byte(image):
        image[bitmap_off + used_block] = 7

    return [
        ("stale-free-inode", stale_free_inode, r"free inode .* stale metadata", "zeroed stale free inode"),
        ("missing-bitmap-mark", missing_bitmap_mark, r"not marked used", "rebuilt bitmap"),
        ("leaked-bitmap-mark", leaked_bitmap_mark, r"unreferenced block", "rebuilt bitmap"),
        ("invalid-bitmap-byte", invalid_bitmap_byte, r"invalid value", "rebuilt bitmap"),
    ]


def unsafe_cases(baseline, fs_start):
    direct_ino, direct_logical, _direct_raw = find_inode_with_direct_block(baseline)
    dup_ino, _dup_logical, dup_raw, free_slot = find_inode_with_direct_block_and_free_slot(baseline)

    def block_out_of_range(image):
        off = inode_offset(fs_start, direct_ino) + 8 + direct_logical * 2
        put_u16(image, off, baseline.expected_blocks + 1)

    def duplicate_block_ref(image):
        off = inode_offset(fs_start, dup_ino) + 8 + free_slot * 2
        put_u16(image, off, dup_raw)

    return [
        ("block-out-of-range", block_out_of_range, r"outside range"),
        ("duplicate-block-ref", duplicate_block_ref, r"referenced multiple times"),
    ]


def write_case_image(out_dir, name, image):
    path = out_dir / f"{name}.img"
    path.write_bytes(bytes(image))
    return path


def verify_repair_case(name, mutate, expected_failure, expected_repair, original, out_dir, fs_start, fs_sectors):
    corrupted = bytearray(original)
    mutate(corrupted)
    failure = expect_check_failure(name, corrupted, fs_start, fs_sectors, expected_failure)
    bad_path = write_case_image(out_dir, name, corrupted)
    fixed_path = out_dir / f"{name}-fixed.img"
    output = run_checker([
        "--image", str(bad_path),
        "--fs-start", str(fs_start),
        "--fs-sectors", str(fs_sectors),
        "--repair",
        "--out", str(fixed_path),
    ])
    if expected_repair not in output:
        fail(f"{name}: repair output did not mention {expected_repair!r}\n{output}")
    check_bytes(fixed_path.read_bytes(), fs_start, fs_sectors)
    ok(f"{name}: repaired ({failure})")


def verify_unsafe_case(name, mutate, expected_failure, original, out_dir, fs_start, fs_sectors):
    corrupted = bytearray(original)
    mutate(corrupted)
    failure = expect_check_failure(name, corrupted, fs_start, fs_sectors, expected_failure)
    bad_path = write_case_image(out_dir, name, corrupted)
    output = run_checker([
        "--image", str(bad_path),
        "--fs-start", str(fs_start),
        "--fs-sectors", str(fs_sectors),
        "--repair",
        "--out", str(out_dir / f"{name}-fixed.img"),
    ], expect_success=False)
    if not re.search(expected_failure, output):
        fail(f"{name}: unsafe repair failed for the wrong reason\n{output}")
    ok(f"{name}: repair refused ({failure})")


def main():
    parser = argparse.ArgumentParser(description="Run minifs repair-mode checks")
    parser.add_argument("--image", default="build/buzzos-test.img", help="formatted raw disk image to mutate")
    parser.add_argument("--fs-start", type=int, default=parse_make_int("FS_START_SECTOR", 512))
    parser.add_argument("--fs-sectors", type=int, default=parse_make_int("FS_SECTORS", 512))
    parser.add_argument("--out-dir", help="optional directory to keep repair fixtures")
    args = parser.parse_args()

    image_path = ROOT / args.image if not Path(args.image).is_absolute() else Path(args.image)
    if not image_path.exists():
        fail(f"image does not exist: {image_path}")
    original = image_path.read_bytes()
    baseline = check_bytes(original, args.fs_start, args.fs_sectors)
    ok(f"baseline image passes: {image_path}")

    run_checker(["--image", str(image_path), "--repair"], expect_success=False)
    run_checker(["--image", str(image_path), "--out", str(image_path.with_suffix(".fixed.img"))], expect_success=False)
    ok("repair CLI mode validation passed")

    if args.out_dir:
        out_dir = ROOT / args.out_dir if not Path(args.out_dir).is_absolute() else Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        for name, mutate, expected_failure, expected_repair in repair_cases(baseline, args.fs_start):
            verify_repair_case(name, mutate, expected_failure, expected_repair, original, out_dir, args.fs_start, args.fs_sectors)
        for name, mutate, expected_failure in unsafe_cases(baseline, args.fs_start):
            verify_unsafe_case(name, mutate, expected_failure, original, out_dir, args.fs_start, args.fs_sectors)
    else:
        with tempfile.TemporaryDirectory(prefix="buzzos-minifs-repair-") as tmp:
            out_dir = Path(tmp)
            for name, mutate, expected_failure, expected_repair in repair_cases(baseline, args.fs_start):
                verify_repair_case(name, mutate, expected_failure, expected_repair, original, out_dir, args.fs_start, args.fs_sectors)
            for name, mutate, expected_failure in unsafe_cases(baseline, args.fs_start):
                verify_unsafe_case(name, mutate, expected_failure, original, out_dir, args.fs_start, args.fs_sectors)

    print("Minifs repair checks passed: 4 safe repairs accepted, 2 unsafe corruptions refused.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
