#!/usr/bin/env python3
"""Check that `filerelic cat` returns exactly the right bytes for every file in
the generated real ext4 fixtures, and that its SHA-256 matches.

Ground truth is the same recipe tools/create_real_ext4_fixture.py used to build
the image, plus an independent `debugfs dump` for the sparse file.

    python3 tools/create_real_ext4_fixture.py 1024
    python3 tools/create_real_ext4_fixture.py 4096
    python3 tools/create_real_ext2_fixture.py        # optional: classic block map
    python3 tools/verify_ext_content.py build/filerelic
"""
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PART_OFFSET = 2048 * 512


def expected_files() -> dict:
    files = {
        "/hello.txt": b"Hello from a real ext4 filesystem.\n",
        "/empty.txt": b"",
        "/docs/readme.txt": b"nested file\n",
    }
    for index in range(130):
        files[f"/file-{index:03d}-padding-name.txt"] = str(index).encode()
    big = bytearray(11 * 64 * 1024 + 8192)
    for island in range(12):
        big[island * 64 * 1024: island * 64 * 1024 + 8192] = bytes([island + 1]) * 8192
    files["/big-sparse.bin"] = bytes(big)
    return files


def main() -> int:
    filerelic = sys.argv[1] if len(sys.argv) > 1 else "build/filerelic"
    bad = checked = 0
    for block_size in (1024, 4096):
        image = ROOT / "testdata" / f"real-ext4-{block_size}.raw"
        if not image.exists():
            print(f"skip {image.name} (generate it with tools/create_real_ext4_fixture.py)")
            continue
        for path, expected in expected_files().items():
            run = subprocess.run([filerelic, "cat", str(image), path], capture_output=True)
            checked += 1
            digest = hashlib.sha256(expected).hexdigest()
            if run.returncode != 0 or run.stdout != expected or digest not in run.stderr.decode():
                bad += 1
                print(f"MISMATCH {image.name} {path}")
        with tempfile.NamedTemporaryFile(suffix=".img") as part, tempfile.NamedTemporaryFile() as out:
            part.write(image.read_bytes()[PART_OFFSET:])
            part.flush()
            subprocess.run(["debugfs", "-R", f"dump /big-sparse.bin {out.name}", part.name],
                           capture_output=True)
            ours = subprocess.run([filerelic, "cat", str(image), "/big-sparse.bin"],
                                  capture_output=True).stdout
            checked += 1
            if Path(out.name).read_bytes() != ours:
                bad += 1
                print(f"MISMATCH {image.name} big-sparse.bin vs debugfs dump")
    # ext2 (indirect blocks), if generated: tools/create_real_ext2_fixture.py
    image = ROOT / "testdata" / "real-ext2-1024.raw"
    if image.exists():
        from importlib.machinery import SourceFileLoader
        maker = SourceFileLoader("mk2", str(ROOT / "tools" / "create_real_ext2_fixture.py")).load_module()
        far = bytes(40 * 1024 * 1024) + b"FAR-DATA" * 100
        expected2 = {
            "/big.bin": maker.deterministic_bytes(5 * 1024 * 1024 + 123, b"filerelic-big"),
            "/far.bin": far,
            "/sub/a.txt": b"ext2 file\n",
        }
        for path, expected in expected2.items():
            run = subprocess.run([filerelic, "cat", str(image), path], capture_output=True)
            checked += 1
            if run.returncode != 0 or run.stdout != expected \
                    or hashlib.sha256(expected).hexdigest() not in run.stderr.decode():
                bad += 1
                print(f"MISMATCH {image.name} {path}")
    else:
        print("skip real-ext2-1024.raw (generate it with tools/create_real_ext2_fixture.py)")
    print(f"{checked} checks, {bad} failures")
    return 1 if bad or checked == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
