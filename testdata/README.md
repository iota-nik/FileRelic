# Practice test data

The files in this folder are generated locally by `../tools/create_practice_image.py`.

Every image is 1 MiB, has a 512-byte Master Boot Record (MBR), contains no real file system, and has no personal data.

| Image | Purpose |
| --- | --- |
| `practice-mbr.raw` | One valid synthetic Linux-type partition beginning at byte offset 512. |
| `practice-mbr-multiple-valid.raw` | Three valid, non-overlapping primary partitions. |
| `practice-mbr-out-of-bounds.raw` | One valid partition and one partition whose declared end exceeds the image boundary. |
| `practice-mbr-extended.raw` | One primary partition, one extended partition, and two valid logical partitions described by an EBR chain. |
| `practice-gpt.raw` | Protective MBR, primary and backup GPT headers, and two valid GPT partition entries. |
| `practice-fs-ntfs.raw` | Synthetic MBR partition with an NTFS boot-sector signature. |
| `practice-fs-apfs.raw` | Synthetic MBR partition with an APFS container signature. |
| `practice-fs-xfs.raw` | Synthetic MBR partition with an XFS superblock signature. |
| `practice-fs-ext4.raw` | Synthetic, non-mountable ext4-shaped image with superblock metadata and a root directory containing `notes.txt` for parser testing. |
| `practice-fs-btrfs.raw` | Synthetic MBR partition with a Btrfs superblock signature. |

They are for learning how FileRelic reads an image and validates an MBR table. Generate them from the repository root:

```bash
python3 tools/create_practice_image.py
```

Then inspect it:

```bash
./build/filerelic inspect-image testdata/practice-mbr.raw
./build/filerelic inspect-image testdata/practice-mbr-multiple-valid.raw
./build/filerelic inspect-image testdata/practice-mbr-out-of-bounds.raw
./build/filerelic inspect-image testdata/practice-mbr-extended.raw
./build/filerelic inspect-image testdata/practice-gpt.raw
./build/filerelic inspect-image testdata/practice-fs-ntfs.raw
./build/filerelic inspect-image testdata/practice-fs-apfs.raw
./build/filerelic inspect-image testdata/practice-fs-xfs.raw
./build/filerelic inspect-image testdata/practice-fs-ext4.raw
./build/filerelic inspect-image testdata/practice-fs-btrfs.raw
```

The expected classifications follow the [MBR validation rules](../docs/MBR_VALIDATION.md): the first two images should be reported as likely MBR tables, while the out-of-bounds image must produce an entry-validation warning.

The NTFS, APFS, XFS, and Btrfs fixtures prove signature identification only. A
matching magic value is evidence, not a full filesystem validation. The ext4
fixture additionally has safe synthetic superblock fields so FileRelic can test
its first filesystem-metadata reader without mounting an image.
