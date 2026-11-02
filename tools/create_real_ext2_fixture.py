#!/usr/bin/env python3
"""Build a REAL ext2 test image (classic block map, no extents) wrapped in an MBR.

ext2/ext3 store a file's blocks as 12 direct pointers plus single, double and
triple indirect blocks. This fixture exercises all of them:

  big.bin      5 MiB + 123 bytes of deterministic pseudo-random data
               (needs a double-indirect block with 1 KiB blocks)
  far.bin      sparse: 40 MiB of hole, then 800 bytes of data
  sub/a.txt    a small file in a subdirectory

Needs Linux with mke2fs (e2fsprogs). Nothing is mounted.
Usage (repo root):  python3 tools/create_real_ext2_fixture.py
Output: testdata/real-ext2-1024.raw
"""
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SECTOR = 512
PART_START_SECTOR = 2048
FS_SIZE = 128 * 1024 * 1024


def deterministic_bytes(length: int, seed: bytes) -> bytes:
    out = bytearray()
    counter = 0
    while len(out) < length:
        out += hashlib.sha256(seed + counter.to_bytes(8, "little")).digest()
        counter += 1
    return bytes(out[:length])


def main() -> int:
    if shutil.which("mke2fs") is None:
        print("mke2fs not found; install e2fsprogs (Linux only)")
        return 1
    repo = Path(__file__).resolve().parent.parent
    output = repo / "testdata" / "real-ext2-1024.raw"
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "content"
        (source / "sub").mkdir(parents=True)
        (source / "big.bin").write_bytes(deterministic_bytes(5 * 1024 * 1024 + 123, b"filerelic-big"))
        with open(source / "far.bin", "wb") as handle:
            handle.seek(40 * 1024 * 1024)
            handle.write(b"FAR-DATA" * 100)
        (source / "sub" / "a.txt").write_text("ext2 file\n")
        fs_image = Path(tmp) / "fs.img"
        with open(fs_image, "wb") as handle:
            handle.truncate(FS_SIZE)
        subprocess.run(["mke2fs", "-q", "-t", "ext2", "-b", "1024", "-d", str(source), str(fs_image)],
                       check=True)
        fs_bytes = fs_image.read_bytes()

    image = bytearray(PART_START_SECTOR * SECTOR + len(fs_bytes))
    image[446:462] = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0x83, b"\xff\xff\xff",
                                 PART_START_SECTOR, len(fs_bytes) // SECTOR)
    image[510:512] = b"\x55\xaa"
    image[PART_START_SECTOR * SECTOR:] = fs_bytes
    output.write_bytes(image)
    print(f"wrote {output} ({len(image)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
