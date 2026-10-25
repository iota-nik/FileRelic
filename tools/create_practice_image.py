#!/usr/bin/env python3
"""Create a safe, tiny raw disk image for FileRelic learning exercises."""

from pathlib import Path
import struct


SECTOR_SIZE = 512
IMAGE_SIZE = 1024 * 1024  # 1 MiB
TOTAL_SECTORS = IMAGE_SIZE // SECTOR_SIZE


def create_image(output_path: Path, partitions: list[tuple[int, int, int]], label: str) -> None:
    image = bytearray(IMAGE_SIZE)

    # Each MBR entry has: boot flag, legacy CHS values, type, start LBA, length.
    for index, (partition_type, start_sector, sector_count) in enumerate(partitions):
        entry_offset = 446 + index * 16
        image[entry_offset : entry_offset + 16] = struct.pack(
            "<B3sB3sII",
            0x00,
            b"\x00\x02\x00",
            partition_type,
            b"\xff\xff\xff",
            start_sector,
            sector_count,
        )
    image[510:512] = b"\x55\xaa"  # MBR signature

    marker = (
        f"FileRelic practice image: {label}.\n".encode()
        + b"It has synthetic MBR entries and no real file system.\n"
        b"It exists only for safe inspection and future parser tests.\n"
    )
    first_partition_start = partitions[0][1] * SECTOR_SIZE
    image[first_partition_start : first_partition_start + len(marker)] = marker

    output_path.write_bytes(image)
    print(f"Created {output_path} ({len(image)} bytes)")


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    testdata = project_root / "testdata"
    testdata.mkdir(exist_ok=True)

    create_image(
        testdata / "practice-mbr.raw",
        [(0x83, 1, TOTAL_SECTORS - 1)],
        "one valid partition",
    )
    create_image(
        testdata / "practice-mbr-multiple-valid.raw",
        [(0x83, 1, 500), (0x83, 501, 500), (0x83, 1001, TOTAL_SECTORS - 1001)],
        "three valid partitions",
    )
    create_image(
        testdata / "practice-mbr-out-of-bounds.raw",
        [(0x83, 1, 500), (0x83, TOTAL_SECTORS - 10, 20)],
        "one valid and one out-of-bounds partition",
    )


if __name__ == "__main__":
    main()
