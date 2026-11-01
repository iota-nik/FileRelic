# FileRelic Architecture: Current Stage

```text
Command-line interface ─┐
                        ├─ InspectionReport ← inspect_image()
Desktop GUI ────────────┘                          │
                                      ┌────────────┴─────────────┐
                                      │  partition_scanner       │ MBR / EBR / GPT
                                      │  filesystem_probe        │ signatures
                                      │  ext_reader              │ ext metadata
                                      └────────────┬─────────────┘
                                                   ▼
                                      ReadOnlySource (FileImageSource)
```

Dependencies point inward: the CLI and GUI know only `image_inspector.h`; the
scanners know `ReadOnlySource` and the report types; nothing reads a file path
except `FileImageSource`.

## Source files (`src/`)

| File | Responsibility |
| --- | --- |
| `image_source.*` | `ReadOnlySource` interface and `FileImageSource`. Every read is bounds- and overflow-checked, capped at 16 MiB, and there is no write method. |
| `byte_utils.h` | Little-endian readers and CRC-32. |
| `scan_context.h` | Internal: the source plus the report being filled, and `trace()`. |
| `partition_scanner.*` | MBR, extended MBR (EBR chain, logical partitions numbered from 5), GPT with CRC checks. |
| `filesystem_probe.*` | Signature detection for NTFS, APFS, XFS, ext-family, Btrfs. A signature is evidence, not support. |
| `ext_reader.*` | Small ext2/3/4 reader: superblock, first group descriptor, root directory (direct blocks only), inodes in the first group. |
| `image_inspector.*` | `inspect_image()`, report sections, `render_report()`, `render_trace()`. |
| `cli_main.cpp` | CLI argument handling and `demo`. |

## Report model

`InspectionReport` holds typed partition records, `FileSystemInfo` (including
`limitations` and an ext `DirectoryListing` of `DirectoryEntryInfo` / `InodeInfo`),
warnings, diagnostics and presentation sections. Unknown values stay explicit
(`std::optional`, empty strings, `limitation` text) rather than becoming zero.

`report.trace` is a step log for `filerelic inspect --verbose` and the GUI
Advanced view. Results are never recovered from it.

## Build

One root `CMakeLists.txt` builds `filerelic_core`, the `filerelic` CLI, and, when
Qt 6 is found, `gui/filerelic-gui`. See [../gui/README.md](../gui/README.md).

```bash
cmake -S . -B build && cmake --build build
./build/filerelic inspect testdata/practice-fs-ext4.raw
./build/filerelic inspect --verbose testdata/practice-fs-ext4.raw
```

## Next boundary

See [ADAPTER_CONTRACT.md](ADAPTER_CONTRACT.md) for the proposed filesystem
adapter interface that a Sleuth Kit backend would implement.
