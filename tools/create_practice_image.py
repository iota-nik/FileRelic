#!/usr/bin/env python3
"""Create a safe, tiny raw disk image for FileRelic learning exercises."""

from pathlib import Path
import struct
import uuid
import zlib


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


def create_filesystem_signature_image(
    output_path: Path, signature_offset: int, signature: bytes
) -> None:
    """Create a safe MBR image containing one deliberately placed signature.

    These fixtures are not mountable filesystems. They prove only that the
    read-only identifier looks at the expected on-disk offset.
    """
    image = bytearray(IMAGE_SIZE)
    partition_start_sector = 1
    write_partition_entry(
        image, 0, 0, 0x83, partition_start_sector, TOTAL_SECTORS - partition_start_sector
    )
    write_mbr_signature(image, 0)

    signature_start = partition_start_sector * SECTOR_SIZE + signature_offset
    image[signature_start : signature_start + len(signature)] = signature
    output_path.write_bytes(image)
    print(f"Created {output_path} ({len(image)} bytes)")


def create_ext4_metadata_image(output_path: Path) -> None:
    """Create a non-mountable ext4-shaped image with a readable superblock."""
    image = bytearray(IMAGE_SIZE)
    partition_start_sector = 1
    partition_start = partition_start_sector * SECTOR_SIZE
    write_partition_entry(
        image, 0, 0, 0x83, partition_start_sector, TOTAL_SECTORS - partition_start_sector
    )
    write_mbr_signature(image, 0)

    superblock = bytearray(1024)
    struct.pack_into("<I", superblock, 0, 128)       # inode count
    struct.pack_into("<I", superblock, 4, 1024)      # block count (low 32 bits)
    struct.pack_into("<I", superblock, 24, 0)        # 1024-byte blocks
    struct.pack_into("<H", superblock, 56, 0xEF53)   # ext-family magic
    struct.pack_into("<H", superblock, 58, 1)        # cleanly unmounted
    struct.pack_into("<I", superblock, 76, 1)        # dynamic revision
    struct.pack_into("<H", superblock, 88, 256)      # inode size
    struct.pack_into("<I", superblock, 96, 0x40)     # extents incompatibility feature
    superblock[104:120] = bytes.fromhex("00112233445566778899aabbccddeeff")
    superblock[120:136] = b"FileRelic ext4".ljust(16, b"\x00")
    superblock_start = partition_start + 1024
    image[superblock_start : superblock_start + len(superblock)] = superblock

    output_path.write_bytes(image)
    print(f"Created {output_path} ({len(image)} bytes)")


def build_gpt_header(
    current_lba: int,
    backup_lba: int,
    first_usable_lba: int,
    last_usable_lba: int,
    partition_entries_lba: int,
    partition_entry_count: int,
    partition_entry_size: int,
    partition_entries_crc32: int,
) -> bytes:
    """Build one 512-byte GPT header sector with a valid header CRC32."""
    header = bytearray(SECTOR_SIZE)
    header[0:8] = b"EFI PART"
    struct.pack_into("<I", header, 8, 0x00010000)  # GPT revision 1.0
    struct.pack_into("<I", header, 12, 92)          # defined GPT header size
    struct.pack_into("<Q", header, 24, current_lba)
    struct.pack_into("<Q", header, 32, backup_lba)
    struct.pack_into("<Q", header, 40, first_usable_lba)
    struct.pack_into("<Q", header, 48, last_usable_lba)
    header[56:72] = uuid.UUID("12345678-1234-5678-9abc-def012345678").bytes_le
    struct.pack_into("<Q", header, 72, partition_entries_lba)
    struct.pack_into("<I", header, 80, partition_entry_count)
    struct.pack_into("<I", header, 84, partition_entry_size)
    struct.pack_into("<I", header, 88, partition_entries_crc32)
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]) & 0xFFFFFFFF)
    return bytes(header)


def write_gpt_entry(
    entries: bytearray,
    index: int,
    start_lba: int,
    end_lba: int,
    name: str,
) -> None:
    """Write a simple GPT entry with a fixed Linux filesystem type GUID."""
    entry_offset = index * 128
    entries[entry_offset : entry_offset + 16] = (
        uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4").bytes_le
    )
    # Fixed per-entry GUIDs keep this generated fixture byte-for-byte reproducible.
    entries[entry_offset + 16 : entry_offset + 32] = uuid.UUID(
        f"12345678-1234-5678-9abc-def0123456{index:02x}"
    ).bytes_le
    struct.pack_into("<Q", entries, entry_offset + 32, start_lba)
    struct.pack_into("<Q", entries, entry_offset + 40, end_lba)
    encoded_name = name.encode("utf-16-le")[:72]
    entries[entry_offset + 56 : entry_offset + 56 + len(encoded_name)] = encoded_name


def create_gpt_image(output_path: Path) -> None:
    """Create a GPT image with two safe synthetic partitions and both headers."""
    image = bytearray(IMAGE_SIZE)
    partition_entry_count = 4
    partition_entry_size = 128
    partition_entries = bytearray(partition_entry_count * partition_entry_size)
    write_gpt_entry(partition_entries, 0, 34, 500, "FileRelic GPT one")
    write_gpt_entry(partition_entries, 1, 501, 1000, "FileRelic GPT two")
    entries_crc32 = zlib.crc32(partition_entries) & 0xFFFFFFFF

    # LBA 0 has a protective MBR so older tools do not mistake the GPT disk as empty.
    write_partition_entry(image, 0, 0, 0xEE, 1, TOTAL_SECTORS - 1)
    write_mbr_signature(image, 0)

    primary_entries_lba = 2
    backup_entries_lba = TOTAL_SECTORS - 2
    image[primary_entries_lba * SECTOR_SIZE : (primary_entries_lba + 1) * SECTOR_SIZE] = partition_entries
    image[backup_entries_lba * SECTOR_SIZE : (backup_entries_lba + 1) * SECTOR_SIZE] = partition_entries

    image[SECTOR_SIZE : 2 * SECTOR_SIZE] = build_gpt_header(
        1, TOTAL_SECTORS - 1, 34, TOTAL_SECTORS - 34,
        primary_entries_lba, partition_entry_count, partition_entry_size, entries_crc32,
    )
    image[(TOTAL_SECTORS - 1) * SECTOR_SIZE : TOTAL_SECTORS * SECTOR_SIZE] = build_gpt_header(
        TOTAL_SECTORS - 1, 1, 34, TOTAL_SECTORS - 34,
        backup_entries_lba, partition_entry_count, partition_entry_size, entries_crc32,
    )

    marker = b"FileRelic GPT practice image; not a real file system.\n"
    image[34 * SECTOR_SIZE : 34 * SECTOR_SIZE + len(marker)] = marker
    image[501 * SECTOR_SIZE : 501 * SECTOR_SIZE + len(marker)] = marker

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
    create_gpt_image(testdata / "practice-gpt.raw")
    create_filesystem_signature_image(
        testdata / "practice-fs-ntfs.raw", 3, b"NTFS    "
    )
    create_filesystem_signature_image(
        testdata / "practice-fs-apfs.raw", 32, b"NXSB"
    )
    create_filesystem_signature_image(
        testdata / "practice-fs-xfs.raw", 0, b"XFSB"
    )
    create_ext4_metadata_image(testdata / "practice-fs-ext4.raw")
    create_filesystem_signature_image(
        testdata / "practice-fs-btrfs.raw", 0x10040, b"_BHRfS_M"
    )


if __name__ == "__main__":
    main()
