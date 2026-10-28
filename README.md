# FileRelic

Cross-platform forensic recovery toolkit for disk images containing NTFS, APFS, ext4, XFS, and Btrfs file systems.

## Start here

- [Development plan and agent work allocation](docs/DEVELOPMENT_PLAN.md)
- [MBR detection and validation rules](docs/MBR_VALIDATION.md)
- [Current CLI/GUI architecture](docs/ARCHITECTURE.md)
- [Detailed session handoff and project context](docs/SESSION_HANDOFF.md)
- [Project roadmap and agent timing](docs/ROADMAP.md)
- [Cyclomatic-complexity PDF](docs/CYCLOMATIC_COMPLEXITY.pdf)
- [detect_file_system() control-flow graph](docs/DETECT_FILE_SYSTEM_CFG.png)
- [Qt desktop-shell build notes](gui/README.md)
- [Original project proposal](references/ORIGINAL_PROJECT_PROPOSAL.pdf)
- [Supplied diagrams](references/diagrams/)

The development plan defines the architecture, workstream boundaries, shared interfaces, integration order, acceptance criteria, and interpretation of the supplied diagrams. It also includes a dated technology review with upstream references. The diagrams and proposal are retained as source material; the plan updates the original XFS/Btrfs and direct-drive assumptions for FileRelic's cross-platform, image-first scope.

## Development status

The current milestone is a read-only image inspector with MBR, extended MBR/EBR, and GPT validation. The shared `InspectionReport` carries typed partition/file-system findings for the CLI and GUI. File-system checks identify probable NTFS, APFS, XFS, ext-family, and Btrfs signatures; the ext-shaped fixture also exercises selected superblock, directory, and inode reads. FileRelic does not yet provide general file inventory or recovery.

## Build the current scaffold

```bash
cmake -S . -B build
cmake --build build
./build/filerelic --help
./build/filerelic inspect-image /path/to/disk-image.raw
./build/filerelic demo
```

`CMakeLists.txt` builds the shared `filerelic_core` library and the `filerelic` CLI. `src/cli_main.cpp` handles `--help`, `--version`, and read-only `inspect-image`; `src/image_inspector.cpp` contains the inspection implementation used by both the CLI and GUI. The inspector accepts regular files only, reports their size, previews up to 16 bytes, validates partition layouts, and recognizes filesystem signatures. The CLI renders a typed summary and retains a detailed trace for findings not modeled yet. FileRelic does not mount or modify the image. The `build/` directory contains generated compiler output and is ignored by Git.

To build and run the Qt GUI, see [gui/README.md](gui/README.md). It links the same core library and runs inspections off the UI thread.

## Safe practice image

You do not need a real disk image to learn the current command. Generate a 1 MiB synthetic MBR image with no personal data, then inspect it:

```bash
python3 tools/create_practice_image.py
./build/filerelic inspect-image testdata/practice-mbr.raw
```

See [the test-data notes](testdata/README.md) for its exact layout.
