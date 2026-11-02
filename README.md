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
- [Filesystem adapter contract (draft)](docs/ADAPTER_CONTRACT.md)
- [Sleuth Kit research brief](docs/TSK_RESEARCH_BRIEF.md)
- [TSK findings summary](docs/TSK_FINDINGS.md)
- [Qt desktop-shell build notes](gui/README.md)
- [Original project proposal](references/ORIGINAL_PROJECT_PROPOSAL.pdf)
- [Supplied diagrams](references/diagrams/)

The development plan defines the architecture, workstream boundaries, shared interfaces, integration order, acceptance criteria, and interpretation of the supplied diagrams. It also includes a dated technology review with upstream references. The diagrams and proposal are retained as source material; the plan updates the original XFS/Btrfs and direct-drive assumptions for FileRelic's cross-platform, image-first scope.

## Development status

The current milestone is a read-only image inspector with MBR, extended MBR/EBR, and GPT validation. The shared `InspectionReport` carries typed partition, filesystem, and ext root-directory/inode findings for the CLI and GUI. File-system checks identify probable NTFS, APFS, XFS, ext-family, and Btrfs signatures; for ext-family images FileRelic reads the superblock, group descriptors, ext4 extent trees, and the root directory (checked against `debugfs` on real `mke2fs` images; see [testdata/README.md](testdata/README.md)). `ls`, `cat` and `extract` list directories and copy files out of ext2/3/4 images with a SHA-256 of the output. `ls --deleted` lists names unlinked from a directory but still on disk, each with an honest status (never a recovery promise). `ls --orphaned` adds a read-only ext-family orphan scan: it reports inodes whose `dtime` marks them as deleted and that no live directory entry points to anymore. `extract --deleted` / `cat --deleted` then try to read such a file back: from the deleted inode's block map, or, when the filesystem erased it, from an older copy of the inode found in the ext4 journal. Blocks that other data has reused are zero-filled, never copied, and the result is PARTIAL. Only ext2/3/4 is covered; NTFS/APFS/XFS/Btrfs are signature-detection only.

## Build

One root build produces the shared core, the CLI, and (when Qt 6 is installed) the GUI:

```bash
cmake -S . -B build
cmake --build build
./build/filerelic help
./build/filerelic inspect testdata/practice-fs-ext4.raw
./build/filerelic inspect --verbose testdata/practice-fs-ext4.raw   # adds the step trace
./build/filerelic ls testdata/real-ext4-1024.raw /docs                 # needs the generated fixture
./build/filerelic ls --deleted testdata/real-ext4-1024.raw /docs      # names deleted from a folder, with status
./build/filerelic ls --orphaned testdata/real-ext4-1024.raw            # inodes deleted from the filesystem and no longer named
python3 tools/create_deleted_fixture.py     # builds testdata/deleted-fixture-1024.raw from a real mke2fs image
./build/filerelic ls --deleted testdata/deleted-fixture-1024.raw /deleted-demo
./build/filerelic extract --deleted testdata/deleted-fixture-1024.raw /deleted-demo/intact.bin ./recovered
./build/filerelic cat testdata/real-ext4-1024.raw /hello.txt
./build/filerelic extract testdata/real-ext4-1024.raw /hello.txt ./recovered
./build/filerelic demo
./build/gui/filerelic-gui                                           # if Qt 6 was found
```

The inspector accepts regular files only and never modifies or mounts the image. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the source layout and [gui/README.md](gui/README.md) for GUI notes. The `build/` directory is ignored by Git.

## Safe practice image

You do not need a real disk image to learn the current command. Generate a 1 MiB synthetic MBR image with no personal data, then inspect it:

```bash
python3 tools/create_practice_image.py
./build/filerelic inspect testdata/practice-mbr.raw
```

See [the test-data notes](testdata/README.md) for its exact layout.
