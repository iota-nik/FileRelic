# Practice test data

`practice-mbr.raw` is generated locally by `../tools/create_practice_image.py`.

It is a 1 MiB raw disk image with:

- a 512-byte Master Boot Record (MBR);
- one synthetic Linux-type partition beginning at byte offset 512; and
- a short marker message at the start of that partition.

It deliberately contains **no file system and no personal data**. It is for learning how FileRelic reads an image and, later, recognizes an MBR partition table. Generate it from the repository root:

```bash
python3 tools/create_practice_image.py
```

Then inspect it:

```bash
./build/filerelic inspect-image testdata/practice-mbr.raw
```
