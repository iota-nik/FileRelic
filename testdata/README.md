# Practice test data

The files in this folder are generated locally by `../tools/create_practice_image.py`.

Every image is a 1 MiB synthetic fixture with no personal data. The partition-table fixtures use a 512-byte MBR sector. The filesystem-signature fixtures are not real mountable filesystems; `practice-fs-ext4.raw` is an intentionally small ext4-shaped teaching fixture with selected metadata and directory records.

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
| `practice-fs-ext4.raw` | Synthetic, non-mountable ext4-shaped image with superblock metadata, a root directory containing `notes.txt`, and inode-12 metadata/data-block pointers for parser testing. |
| `practice-fs-btrfs.raw` | Synthetic MBR partition with a Btrfs superblock signature. |

They are for learning how FileRelic reads an image and validates an MBR table. Generate them from the repository root:

```bash
python3 tools/create_practice_image.py
```

Then inspect it:

```bash
./build/filerelic inspect testdata/practice-mbr.raw
./build/filerelic inspect testdata/practice-mbr-multiple-valid.raw
./build/filerelic inspect testdata/practice-mbr-out-of-bounds.raw
./build/filerelic inspect testdata/practice-mbr-extended.raw
./build/filerelic inspect testdata/practice-gpt.raw
./build/filerelic inspect testdata/practice-fs-ntfs.raw
./build/filerelic inspect testdata/practice-fs-apfs.raw
./build/filerelic inspect testdata/practice-fs-xfs.raw
./build/filerelic inspect testdata/practice-fs-ext4.raw
./build/filerelic inspect testdata/practice-fs-btrfs.raw
```

The expected classifications follow the [MBR validation rules](../docs/MBR_VALIDATION.md): the first two images should be reported as likely MBR tables, while the out-of-bounds image must produce an entry-validation warning.

The NTFS, APFS, XFS, and Btrfs fixtures prove signature identification only. A
matching magic value is evidence, not a full filesystem validation. The ext4
fixture additionally has safe synthetic superblock fields so FileRelic can test
its first filesystem-metadata reader without mounting an image.

## Real ext4 images (generated, not committed)

`python3 tools/create_real_ext4_fixture.py 1024` (or `4096`) builds
`testdata/real-ext4-<size>.raw`, a genuine fsck-clean ext4 filesystem made by
`mke2fs` (Linux, e2fsprogs) inside an MBR partition, with known files including a
sparse file that needs a multi-level extent tree. Check FileRelic against the
independent `debugfs` tool with:

```bash
python3 tools/verify_real_ext4.py build/filerelic testdata/real-ext4-1024.raw
```

### Trying `ls --deleted` on the real fixture

The fixture is a whole disk image: the ext4 filesystem starts 1 MiB in (partition
sector 2048). `debugfs` therefore needs an offset, and you should work on a copy so
the fixture stays pristine:

```bash
cp testdata/real-ext4-1024.raw /tmp/deleted-test.raw
debugfs -w -R "rm /hello.txt" "/tmp/deleted-test.raw?offset=1048576"
./build/filerelic ls --deleted /tmp/deleted-test.raw          # root folder
./build/filerelic ls --deleted /tmp/deleted-test.raw /docs    # a sub-folder
```

(`debugfs rm` keeps the file's block list, so the status is usually "data likely
intact". A real kernel delete often erases it, giving "the filesystem erased where
the data was".) `debugfs ls -d "<image>?offset=1048576" /` is an independent way to
list the same deleted names; entries whose inode number was zeroed appear only in
FileRelic.

File contents are checked with `python3 tools/verify_ext_content.py build/filerelic`
(ground truth + `debugfs dump` + SHA-256). `python3 tools/create_real_ext2_fixture.py`
adds `real-ext2-1024.raw` (about 130 MB, classic indirect blocks) to those checks.
