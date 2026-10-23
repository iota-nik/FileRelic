# FileRelic Project Workspace

Cross-platform forensic recovery toolkit for disk images containing NTFS, APFS, ext4, XFS, and Btrfs file systems.

## Start here

- [Development plan and agent work allocation](docs/DEVELOPMENT_PLAN.md)
- [Original project proposal](references/ORIGINAL_PROJECT_PROPOSAL.pdf)
- [Supplied diagrams](references/diagrams/)

The development plan defines the architecture, workstream boundaries, shared interfaces, integration order, acceptance criteria, and interpretation of the supplied diagrams. It also includes a dated technology review with upstream references. The diagrams and proposal are retained as source material; the plan updates the original XFS/Btrfs and direct-drive assumptions for FileRelic's cross-platform, image-first scope.

## Development status

The source repository currently contains a minimal C++17 command-line scaffold. Qt 6 is not detected in the current Linux environment, so the first build uses only the C++ standard library; Qt will be introduced in a later step after the CLI and core boundaries are clear.

## Build the current scaffold

```bash
cmake -S . -B build
cmake --build build
./build/filerelic --help
```

`CMakeLists.txt` describes how to build the `filerelic` executable. `src/main.cpp` handles `--help` and `--version`. The `build/` directory contains generated compiler output and is ignored by Git.
