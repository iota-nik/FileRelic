#!/usr/bin/env python3
"""Create a safe, tiny raw disk image for FileRelic learning exercises."""

from pathlib import Path
import struct


SECTOR_SIZE = 512
IMAGE_SIZE = 1024 * 1024  # 1 MiB
TOTAL_SECTORS = IMAGE_SIZE // SECTOR_SIZE


def write_partition_entry(
    image: bytearray,
    table_sector: int,
    index: int,
    partition_type: int,
    start_sector: int,
    sector_count: int,
) -> None:
    """Write one 16-byte MBR or EBR partition entry."""
    entry_offset = table_sector * SECTOR_SIZE + 446 + index * 16
    image[entry_offset : entry_offset + 16] = struct.pack(
        "<B3sB3sII",
        0x00,
        b"\x00\x02\x00",
        partition_type,
        b"\xff\xff\xff",
        start_sector,
        sector_count,
    )


def write_mbr_signature(image: bytearray, table_sector: int) -> None:
    signature_offset = table_sector * SECTOR_SIZE + 510
    image[signature_offset : signature_offset + 2] = b"\x55\xaa"


def create_image(output_path: Path, partitions: list[tuple[int, int, int]], label: str) -> None:
    image = bytearray(IMAGE_SIZE)

    # Each MBR entry has: boot flag, legacy CHS values, type, start LBA, length.
    for index, (partition_type, start_sector, sector_count) in enumerate(partitions):
        write_partition_entry(image, 0, index, partition_type, start_sector, sector_count)
    write_mbr_signature(image, 0)

    marker = (
        f"FileRelic practice image: {label}.\n".encode()
        + b"It has synthetic MBR entries and no real file system.\n"
        b"It exists only for safe inspection and future parser tests.\n"
    )
    first_partition_start = partitions[0][1] * SECTOR_SIZE
    image[first_partition_start : first_partition_start + len(marker)] = marker

    output_path.write_bytes(image)
    print(f"Created {output_path} ({len(image)} bytes)")


def create_extended_partition_image(output_path: Path) -> None:
    """Create an MBR with one primary and two logical partitions."""
    image = bytearray(IMAGE_SIZE)

    # Main MBR: primary partition at sectors 1–200; extended container at 201–1200.
    write_partition_entry(image, 0, 0, 0x83, 1, 200)
    write_partition_entry(image, 0, 1, 0x0F, 201, 1000)
    write_mbr_signature(image, 0)

    # EBR 1 is at the start of the extended partition, sector 201.
    # Its logical partition starts one sector later and occupies sectors 202–501.
    write_partition_entry(image, 201, 0, 0x83, 1, 300)
    # The next EBR is at sector 502: 301 sectors from the extended partition base.
    write_partition_entry(image, 201, 1, 0x0F, 301, 699)
    write_mbr_signature(image, 201)

    # EBR 2 describes the final logical partition at sectors 503–802.
    write_partition_entry(image, 502, 0, 0x83, 1, 300)
    write_mbr_signature(image, 502)

    marker = b"FileRelic extended-partition practice image; not a real file system.\n"
    image[SECTOR_SIZE : SECTOR_SIZE + len(marker)] = marker
    image[202 * SECTOR_SIZE : 202 * SECTOR_SIZE + len(marker)] = marker
    image[503 * SECTOR_SIZE : 503 * SECTOR_SIZE + len(marker)] = marker

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
    create_extended_partition_image(testdata / "practice-mbr-extended.raw")


if __name__ == "__main__":
    main()
