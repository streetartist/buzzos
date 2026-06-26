#!/usr/bin/env python3
import argparse
import contextlib
import io
import re
import struct
from pathlib import Path

from check_minifs import (
    FsError,
    MiniFsImage,
    MINIFS_FILE,
    MINIFS_INODES,
    MINIFS_ROOT_INO,
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


def expect_failure(name, image, fs_start, fs_sectors, expected):
    try:
        check_bytes(image, fs_start, fs_sectors)
    except FsError as exc:
        message = str(exc)
        if not re.search(expected, message):
            fail(f"{name}: expected /{expected}/, got: {message}")
        ok(f"{name}: rejected ({message})")
        return
    fail(f"{name}: corrupted image unexpectedly passed")


def put_u16(image, offset, value):
    struct.pack_into("<H", image, offset, value)


def put_u32(image, offset, value):
    struct.pack_into("<I", image, offset, value)


def inode_offset(fs_start, ino):
    return (fs_start + 1 + ino) * SECTOR_SIZE


def bitmap_offset(fs_start):
    return (fs_start + 1 + MINIFS_INODES) * SECTOR_SIZE


def find_inode_with_direct_block(fs):
    for inode in fs.inodes:
        if not inode["used"]:
            continue
        for logical, raw in enumerate(inode["blocks"]):
            if raw:
                return inode["ino"], logical, raw
    fail("baseline image has no direct block references")


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


def find_non_root_inode(fs):
    for inode in fs.inodes:
        if inode["used"] and inode["ino"] != MINIFS_ROOT_INO:
            return inode["ino"]
    fail("baseline image has no non-root used inode")


def make_cases(baseline, fs_start):
    super_off = fs_start * SECTOR_SIZE
    bitmap_off = bitmap_offset(fs_start)
    used_block = find_used_block(baseline)
    free_block = find_free_block(baseline)
    free_inode = find_free_inode(baseline)
    child_inode = find_non_root_inode(baseline)
    direct_ino, direct_logical, direct_raw = find_inode_with_direct_block(baseline)

    def bad_magic(image):
        put_u32(image, super_off, 0)

    def bad_inode_count(image):
        put_u32(image, super_off + 4, MINIFS_INODES - 1)

    def invalid_bitmap_value(image):
        image[bitmap_off + used_block] = 2

    def missing_bitmap_mark(image):
        image[bitmap_off + used_block] = 0

    def leaked_bitmap_mark(image):
        image[bitmap_off + free_block] = 1

    def root_is_file(image):
        image[inode_offset(fs_start, MINIFS_ROOT_INO) + 1] = MINIFS_FILE

    def block_out_of_range(image):
        off = inode_offset(fs_start, direct_ino) + 8 + direct_logical * 2
        put_u16(image, off, baseline.expected_blocks + 1)

    def duplicate_block_ref(image):
        blocks = baseline.inodes[direct_ino]["blocks"]
        target_slot = -1
        for i, raw in enumerate(blocks):
            if i != direct_logical and raw == 0:
                target_slot = i
                break
        if target_slot < 0:
            fail("baseline direct block array has no free slot for duplicate test")
        off = inode_offset(fs_start, direct_ino) + 8 + target_slot * 2
        put_u16(image, off, direct_raw)

    def stale_free_inode(image):
        off = inode_offset(fs_start, free_inode)
        put_u32(image, off + 4, 1)

    def bad_parent(image):
        off = inode_offset(fs_start, child_inode)
        put_u16(image, off + 2, MINIFS_INODES - 1)

    return [
        ("bad-super-magic", bad_magic, r"bad minifs magic"),
        ("bad-inode-count", bad_inode_count, r"inode_count"),
        ("invalid-bitmap-byte", invalid_bitmap_value, r"bitmap block .* invalid value"),
        ("missing-bitmap-mark", missing_bitmap_mark, r"not marked used"),
        ("leaked-bitmap-mark", leaked_bitmap_mark, r"unreferenced block"),
        ("root-is-file", root_is_file, r"root inode"),
        ("block-out-of-range", block_out_of_range, r"outside range"),
        ("duplicate-block-ref", duplicate_block_ref, r"referenced multiple times"),
        ("stale-free-inode", stale_free_inode, r"free inode .* stale metadata"),
        ("bad-parent", bad_parent, r"invalid parent|parent .* is not a directory"),
    ]


def main():
    parser = argparse.ArgumentParser(description="Run negative corruption checks against the minifs checker")
    parser.add_argument("--image", default="build/buzzos-test.img", help="formatted raw disk image to mutate in memory")
    parser.add_argument("--fs-start", type=int, default=parse_make_int("FS_START_SECTOR", 512))
    parser.add_argument("--fs-sectors", type=int, default=parse_make_int("FS_SECTORS", 512))
    parser.add_argument("--out-dir", help="optional directory to write corrupted image fixtures")
    args = parser.parse_args()

    path = ROOT / args.image if not Path(args.image).is_absolute() else Path(args.image)
    if not path.exists():
        fail(f"image does not exist: {path}")
    original = bytearray(path.read_bytes())
    baseline = check_bytes(original, args.fs_start, args.fs_sectors)
    ok(f"baseline image passes: {path}")

    out_dir = None
    if args.out_dir:
        out_dir = ROOT / args.out_dir if not Path(args.out_dir).is_absolute() else Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)

    cases = make_cases(baseline, args.fs_start)
    for name, mutate, expected in cases:
        corrupted = bytearray(original)
        mutate(corrupted)
        if out_dir:
            (out_dir / f"{name}.img").write_bytes(corrupted)
        expect_failure(name, corrupted, args.fs_start, args.fs_sectors, expected)

    print(f"Minifs negative checks passed: {len(cases)} corruptions rejected.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
