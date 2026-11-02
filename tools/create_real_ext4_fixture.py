#!/usr/bin/env python3
"""Build a REAL (mountable, fsck-clean) ext4 test image wrapped in an MBR.

Unlike testdata/practice-fs-ext4.raw (hand-made, tiny), this uses mke2fs from
e2fsprogs to format a genuine ext4 filesystem populated with known files:

  hello.txt          small file, one extent
  empty.txt          zero-length file
  big-sparse.bin     sparse file with 12 separate data islands -> more extents
                     than fit in the inode, so the extent tree has an index level
  docs/readme.txt    a file inside a sub-directory
  file-000-padding-name.txt ... file-129-...   130 files in the ROOT directory,
                     so the root directory itself spans several blocks

Nothing is mounted. Needs Linux with mke2fs/debugfs (package e2fsprogs).

Usage (from the repository root):
    python3 tools/create_real_ext4_fixture.py [block_size]   # 1024 (default) or 4096
Output: testdata/real-ext4-<block_size>.raw
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SECTOR = 512
PART_START_SECTOR = 2048          # 1 MiB offset, like modern partitioning tools
FS_SIZE = 8 * 1024 * 1024


def populate(root: Path) -> None:
    (root / "hello.txt").write_text("Hello from a real ext4 filesystem.\n")
    (root / "empty.txt").write_bytes(b"")
    (root / "docs").mkdir()
    (root / "docs" / "readme.txt").write_text("nested file\n")
    for index in range(130):
        (root / f"file-{index:03d}-padding-name.txt").write_text(str(index))
    with open(root / "big-sparse.bin", "wb") as handle:
        for island in range(12):
            handle.seek(island * 64 * 1024)
            handle.write(bytes([island + 1]) * 8192)


def main() -> int:
    block_size = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
    if block_size not in (1024, 4096):
        print("block size must be 1024 or 4096")
        return 2
    if shutil.which("mke2fs") is None:
        print("mke2fs not found; install e2fsprogs (Linux only)")
        return 1

    repo = Path(__file__).resolve().parent.parent
    output = repo / "testdata" / f"real-ext4-{block_size}.raw"
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "content"
        source.mkdir()
        populate(source)
        fs_image = Path(tmp) / "fs.img"
        with open(fs_image, "wb") as handle:
            handle.truncate(FS_SIZE)
        subprocess.run(
            ["mke2fs", "-q", "-t", "ext4", "-b", str(block_size), "-L", "FileRelicReal",
             "-U", "00112233-4455-6677-8899-aabbccddeeff", "-O", "^has_journal",
             "-E", "root_owner=0:0", "-d", str(source), str(fs_image)],
            check=True,
        )
        fs_bytes = fs_image.read_bytes()

    total_sectors = PART_START_SECTOR + len(fs_bytes) // SECTOR
    image = bytearray(total_sectors * SECTOR)
    entry = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0x83, b"\xff\xff\xff",
                        PART_START_SECTOR, len(fs_bytes) // SECTOR)
    image[446:462] = entry
    image[510:512] = b"\x55\xaa"
    image[PART_START_SECTOR * SECTOR:] = fs_bytes
    output.write_bytes(image)
    print(f"wrote {output} ({len(image)} bytes); verify with: e2fsck -fn on the partition, "
          f"or debugfs -R 'ls -l' on a copy of it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
