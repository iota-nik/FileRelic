# Practice test data

The files in this folder are generated locally by `../tools/create_practice_image.py`.

Every image is 1 MiB, has a 512-byte Master Boot Record (MBR), contains no real file system, and has no personal data.

| Image | Purpose |
| --- | --- |
| `practice-mbr.raw` | One valid synthetic Linux-type partition beginning at byte offset 512. |
| `practice-mbr-multiple-valid.raw` | Three valid, non-overlapping primary partitions. |
| `practice-mbr-out-of-bounds.raw` | One valid partition and one partition whose declared end exceeds the image boundary. |
| `practice-mbr-extended.raw` | One primary partition, one extended partition, and two valid logical partitions described by an EBR chain. |

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
```

The expected classifications follow the [MBR validation rules](../docs/MBR_VALIDATION.md): the first two images should be reported as likely MBR tables, while the out-of-bounds image must produce an entry-validation warning.
