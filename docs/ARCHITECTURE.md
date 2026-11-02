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
| `sha256.*` | Streaming SHA-256 (verified against hashlib/sha256sum on boundary sizes and a 5 MiB file). |
| `volume_access.*` | High-level `list_path`, `read_file`, `extract_file`: pick the partition, open the ext volume, stream/hash/extract. The CLI and later the GUI call only these. |
| `ext_reader.*` | `ExtVolume`: ext2/3/4 reader. Superblock, 32/64-bit group descriptors, inodes in any group, ext4 extent trees, ext2/3 block maps (direct + indirect), directory scan, path lookup, file-content streaming (holes and unwritten extents read as zeros; hole total capped at 1 GiB; overlapping extents rejected). No deleted entries, inline data, encryption, or symlink targets yet. |
| `report_format.*` | Plain-language wording helpers shared by every interface: human sizes ("1.0 MiB"), partition-type names, filesystem descriptions, file/folder wording. Change wording here, once. |
| `report_sections.*` | `build_sections()`: turns the typed results into `ReportSection`s (title, `Severity`, one-sentence summary, label/value `ReportRow`s, notes). |
| `report_text.cpp` | `render_report()`: draws the sections as terminal text. Technical rows, `(detail)` text and the step trace appear only with `-v`. |
| `image_inspector.*` | `inspect_image()` (scan, then `build_sections`) and `render_trace()`. |
| `cli_main.cpp` | CLI argument handling and `demo`. |

## Report model

**One wording, two renderers.** The core builds human-readable `ReportSection`s once.
The CLI prints them as text (`report_text.cpp`); the GUI draws each as a card
coloured by `Severity` (`gui/main_window.cpp`). Neither interface parses or
re-words anything. `ReportRow::technical` rows (sector numbers, volume IDs,
features) and `ReportRow::detail` are hidden by default: `-v` in the CLI, the
"Show technical details" checkbox in the GUI. `ls`/`cat`/`extract` use the same
size and file/folder wording (`ls -v` adds inode numbers).

`InspectionReport` holds typed partition records, `FileSystemInfo` (including
`limitations` and an ext `DirectoryListing` of `DirectoryEntryInfo` / `InodeInfo` / `ExtentInfo`),
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

## CLI conventions

Shape: `filerelic <command> [options] [arguments]`. Commands: `inspect`, `ls`, `cat`, `extract`,
`demo`, `version`, `help`. Every command accepts `-h/--help`. Results go to stdout;
usage errors and failed operations go to stderr. Exit codes: 0 success,
1 operation failed (e.g. unreadable image), 2 usage error. New commands should follow the same rules. `cat` writes file bytes to stdout and its Result/Size/SHA-256 lines to stderr. `inspect-image` is a
deprecated alias for `inspect`.

## Next boundary

Real-image validation: `tools/create_real_ext4_fixture.py` and `tools/verify_real_ext4.py` (compare with `debugfs`).

See [ADAPTER_CONTRACT.md](ADAPTER_CONTRACT.md) for the proposed filesystem
adapter interface that a Sleuth Kit backend would implement.
