#!/usr/bin/env python3
"""Cross-check FileRelic's ext root-directory output against debugfs (e2fsprogs).

debugfs is an independent implementation, so agreement here is real evidence.
Compares, for every root entry: inode number, size, extent count, and the first
physical block. Usage (repo root, after building):

    python3 tools/create_real_ext4_fixture.py 1024
    python3 tools/verify_real_ext4.py build/filerelic testdata/real-ext4-1024.raw
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PART_OFFSET = 2048 * 512


def debugfs(image: str, command: str) -> str:
    return subprocess.run(["debugfs", "-R", command, image],
                          capture_output=True, text=True, check=True).stdout


def truth(partition_image: str) -> dict:
    result = {}
    for line in debugfs(partition_image, "ls -l").splitlines():
        match = re.match(r"\s*(\d+)\s+\d+\s+\(\d+\)\s+\d+\s+\d+\s+(\d+)\s+\S+\s+\S+\s+(.+)$", line)
        if match:
            result[match.group(3).strip()] = int(match.group(1))
    facts = {}
    for name, inode in result.items():
        if name in (".", ".."):
            continue
        out = debugfs(partition_image, f"stat <{inode}>")
        size = int(re.search(r"Size: (\d+)", out).group(1))
        tail = out.split("EXTENTS:", 1)[1].strip().splitlines() if "EXTENTS:" in out else []
        ext_line = tail[0] if tail else ""
        leaves = [m for m in re.findall(r"\((ETB\d+|[\d\-]+)\):([\d\-]+)", ext_line) if not m[0].startswith("ETB")]
        first = int(leaves[0][1].split("-")[0]) if leaves else None
        facts[name] = (inode, size, len(leaves), first)
    return facts


def ours(filerelic: str, image: str) -> dict:
    text = subprocess.run([filerelic, "inspect", image], capture_output=True, text=True).stdout
    facts = {}
    for line in text.splitlines():
        m = re.match(r"\s+(\S.*?) \(inode (\d+)\): (.*)$", line)
        if not m or m.group(1) in (".", ".."):
            continue
        rest = m.group(3)
        size = int(re.search(r"(\d+) bytes", rest).group(1))
        ext = re.search(r"(\d+) extents?", rest)
        first = re.search(r"first data block (\d+)", rest)
        facts[m.group(1)] = (int(m.group(2)), size, int(ext.group(1)) if ext else 0,
                             int(first.group(1)) if first else None)
    return facts


def main() -> int:
    filerelic, image = sys.argv[1], sys.argv[2]
    with tempfile.NamedTemporaryFile(suffix=".img") as part:
        part.write(Path(image).read_bytes()[PART_OFFSET:])
        part.flush()
        expected = truth(part.name)
    actual = ours(filerelic, image)
    bad = 0
    for name in sorted(set(expected) | set(actual)):
        if expected.get(name) != actual.get(name):
            bad += 1
            print(f"MISMATCH {name}: debugfs={expected.get(name)} filerelic={actual.get(name)}")
    print(f"{len(expected)} entries checked against debugfs, {bad} mismatches")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
