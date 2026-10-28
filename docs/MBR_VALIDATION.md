# MBR Detection and Validation Rules

An MBR signature (`55 aa` at bytes 510–511) is evidence, not proof, that an image has an MBR partition table. FileRelic uses the following seven rules before reporting an image as a likely MBR table.

1. **Minimum size:** the image must contain at least one 512-byte sector.
2. **Read all primary entries:** inspect all four 16-byte entries, located from byte 446 through byte 509. Do not decide based only on partition entry 1.
3. **Validate boundaries:** for each populated entry, calculate its start and end. Its complete byte range must fit inside the source image.
4. **Check overlaps:** valid primary partition ranges must not overlap. An overlap is a warning, since it may indicate corruption or a deliberately unusual layout.
5. **Check entry consistency:** a populated entry must have both a non-zero type and a non-zero sector count. The type is a hint about intended usage, not proof of its file system.
6. **Inspect claimed contents:** when a partition range is valid, look at the expected offsets for supported file-system signatures. A missing signature does not prove the partition is invalid; it may contain an unsupported, encrypted, damaged, or unformatted file system.
7. **Report confidence honestly:** say `likely MBR partition table` only when the signature and populated entries pass the basic checks. Otherwise report the signature and each warning without claiming a valid table.

## MBR capacity

An MBR has room for four primary entries because it reserves 64 bytes for them: four entries of 16 bytes each. An old workaround for more partitions is an **extended partition**. One primary entry points to an Extended Boot Record (EBR), which describes one logical partition and can point to another EBR.

```text
MBR
├─ primary partition 1
├─ primary partition 2
└─ extended partition
   ├─ EBR → logical partition 1
   └─ EBR → logical partition 2 → ...
```

FileRelic walks valid EBR chains with a 128-record safety limit, image/extended-container boundary checks, signature checks, and loop detection. GPT is the newer alternative: it keeps its partition entries outside the MBR and commonly supports far more entries.

## Current scope

The shared inspection core reads regular image files only and does not mount or modify them. Its filesystem checks are lightweight identification clues for NTFS, APFS, ext-family, XFS, and Btrfs. The `InspectionReport` returns typed partition bounds and validation warnings to both the CLI and Qt GUI. Full filesystem inventory and recovery are later milestones.
