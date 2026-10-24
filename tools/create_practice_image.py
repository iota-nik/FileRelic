#!/usr/bin/env python3
"""Create a safe, tiny raw disk image for FileRelic learning exercises."""

from pathlib import Path
import struct


SECTOR_SIZE = 512
IMAGE_SIZE = 1024 * 1024  # 1 MiB
PARTITION_START_SECTOR = 1
PARTITION_SECTORS = (IMAGE_SIZE // SECTOR_SIZE) - PARTITION_START_SECTOR


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    output_path = project_root / "testdata" / "practice-mbr.raw"
    output_path.parent.mkdir(exist_ok=True)

    image = bytearray(IMAGE_SIZE)

    # MBR partition entry: boot flag, CHS placeholders, type, start LBA, size.
    partition_entry_offset = 446
    image[partition_entry_offset : partition_entry_offset + 16] = struct.pack(
        "<B3sB3sII",
        0x00,                 # not bootable
        b"\x00\x02\x00",    # legacy CHS start placeholder
        0x83,                 # Linux partition type marker
        b"\xff\xff\xff",    # legacy CHS end placeholder
        PARTITION_START_SECTOR,
        PARTITION_SECTORS,
    )
    image[510:512] = b"\x55\xaa"  # MBR signature

    marker_offset = PARTITION_START_SECTOR * SECTOR_SIZE
    marker = (
        b"FileRelic practice image.\n"
        b"It has one synthetic MBR partition and no real file system.\n"
        b"It exists only for safe inspection and future parser tests.\n"
    )
    image[marker_offset : marker_offset + len(marker)] = marker

    output_path.write_bytes(image)
    print(f"Created {output_path} ({len(image)} bytes)")


if __name__ == "__main__":
    main()
