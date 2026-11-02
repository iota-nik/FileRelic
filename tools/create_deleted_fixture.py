#!/usr/bin/env python3
"""Build testdata/deleted-fixture-<block>.raw: a real ext4 image (with a journal) in
which known files were deleted in four different ways, to try `filerelic ls --deleted`
and `filerelic extract --deleted` against ground truth.

Nothing is mounted. Needs Linux with mke2fs and debugfs (package e2fsprogs).
The files live in /deleted-demo. After deletion:

  intact.bin       deleted normally; the inode still lists its blocks
                   -> extract --deleted: Complete, SHA-256 equals the original
  journal.bin      deleted AND its block map erased (as a kernel delete often does),
                   but the inode was journaled while the file was alive
                   -> extract --deleted: Complete, recovered from the journal
  overwritten.bin  deleted, then the block bitmap was told that the first half of its blocks
                   are in use again (what happens when another file takes them)
                   -> extract --deleted: PARTIAL, those blocks become zeros
  lost.bin         deleted and its block map erased, nothing in the journal
                   -> extract --deleted: FAILED with an explanation
  empty.txt        an empty file, deleted
  keep.txt         never deleted (a control)

The original SHA-256 values are printed so you can compare them with FileRelic's output.
Usage: python3 tools/create_deleted_fixture.py [1024|4096]
"""

import hashlib
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SECTOR = 512
PART_START_SECTOR = 2048          # 1 MiB offset, like modern partitioning tools
FS_SIZE = 16 * 1024 * 1024
FILES = {                         # name -> size in bytes
    "intact.bin": 20000,
    "journal.bin": 30000,
    "overwritten.bin": 15000,
    "lost.bin": 10000,
    "empty.txt": 0,
    "keep.txt": 2500,
}


def debugfs(image: Path, command: str, write: bool = False) -> str:
    args = ["debugfs"] + (["-w"] if write else []) + ["-R", command, str(image)]
    done = subprocess.run(args, capture_output=True, text=True)
    return done.stdout + done.stderr


def inode_of(image: Path, path: str) -> int:
    match = re.search(r"Inode: (\d+)", debugfs(image, f"stat {path}"))
    if not match:
        raise RuntimeError(f"cannot find {path}")
    return int(match.group(1))


def inode_table_block(image: Path, path: str) -> int:
    match = re.search(r"located at block (\d+)", debugfs(image, f"imap {path}"))
    if not match:
        raise RuntimeError(f"imap failed for {path}")
    return int(match.group(1))


def erase_block_map(image: Path, inode: int) -> None:
    # i_block[0] holds the extent header's magic (low 16 bits) and entry count (high 16
    # bits). Entry count 0 means "no extents", which is how ext4 leaves a deleted inode.
    debugfs(image, f"set_inode_field <{inode}> block[0] 0xF30A", write=True)


def main() -> int:
    block_size = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
    if block_size not in (1024, 4096):
        print("block size must be 1024 or 4096")
        return 2
    if shutil.which("mke2fs") is None or shutil.which("debugfs") is None:
        print("mke2fs/debugfs not found; install e2fsprogs (Linux only)")
        return 1

    repo = Path(__file__).resolve().parent.parent
    output = repo / "testdata" / f"deleted-fixture-{block_size}.raw"
    rng = random.Random(20261102)               # same bytes every run
    contents = {name: rng.randbytes(size) for name, size in FILES.items()}

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        source = tmp_path / "content"
        (source / "deleted-demo").mkdir(parents=True)
        for name, data in contents.items():
            (source / "deleted-demo" / name).write_bytes(data)

        image = tmp_path / "fs.img"
        with open(image, "wb") as handle:
            handle.truncate(FS_SIZE)
        subprocess.run(
            ["mke2fs", "-q", "-t", "ext4", "-b", str(block_size), "-L", "FileRelicDel",
             "-U", "00112233-4455-6677-8899-aabbccddeeff", "-E", "root_owner=0:0",
             "-d", str(source), str(image)],
            check=True,
        )
        if "has_journal" not in debugfs(image, "features"):
            print("this mke2fs did not create a journal; the journal case cannot be built")
            return 1

        demo = "/deleted-demo"
        # lost.bin: delete and erase the map BEFORE anything is journaled.
        lost = inode_of(image, f"{demo}/lost.bin")
        debugfs(image, f"rm {demo}/lost.bin", write=True)
        erase_block_map(image, lost)

        # journal.bin: journal the inode-table block while the file is still alive.
        journal_inode = inode_of(image, f"{demo}/journal.bin")
        block = inode_table_block(image, f"{demo}/journal.bin")
        copy = tmp_path / "itable.blk"
        copy.write_bytes(image.read_bytes()[block * block_size:(block + 1) * block_size])
        script = tmp_path / "journal.cmds"
        script.write_text(f"jo\njw -b {block} {copy}\njc\n")
        subprocess.run(["debugfs", "-w", "-f", str(script), str(image)],
                       capture_output=True, check=True)
        debugfs(image, f"rm {demo}/journal.bin", write=True)
        erase_block_map(image, journal_inode)

        # overwritten.bin: find where its data lives before deleting it.
        listing = debugfs(image, f"ex {demo}/overwritten.bin")
        extent = re.search(r"\s(\d+)\s+-\s+(\d+)\s+(\d+)\s+-\s+(\d+)\s+(\d+)", listing)
        if not extent:
            raise RuntimeError("cannot read overwritten.bin's extent")
        first_physical = int(extent.group(3))
        reused = max(1, int(extent.group(5)) // 2)
        for name in ("intact.bin", "overwritten.bin", "empty.txt"):
            debugfs(image, f"rm {demo}/{name}", write=True)
        # Mark the first half of its blocks as allocated again. FileRelic only trusts the bitmap, so
        # this is exactly what another file taking those blocks looks like to it.
        # (e2fsck would call them "in use but unowned"; this is an evidence fixture.)
        debugfs(image, f"setb {first_physical} {reused}", write=True)

        fs_bytes = image.read_bytes()

    total_sectors = PART_START_SECTOR + len(fs_bytes) // SECTOR
    disk = bytearray(total_sectors * SECTOR)
    entry = bytearray(16)
    entry[4] = 0x83                                       # Linux filesystem
    struct.pack_into("<II", entry, 8, PART_START_SECTOR, len(fs_bytes) // SECTOR)
    disk[446:462] = entry
    disk[510:512] = b"\x55\xaa"
    disk[PART_START_SECTOR * SECTOR:] = fs_bytes
    output.write_bytes(disk)

    print(f"Wrote {output} ({output.stat().st_size} bytes)\n")
    print("Original SHA-256 values (compare with FileRelic's output):")
    for name, data in contents.items():
        print(f"  {hashlib.sha256(data).hexdigest()}  {len(data):>6} bytes  /deleted-demo/{name}")
    print(f"\nTry:\n  ./build/filerelic ls --deleted {output.relative_to(repo)} /deleted-demo")
    print(f"  ./build/filerelic extract --deleted {output.relative_to(repo)} "
          f"/deleted-demo/intact.bin ./recovered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
