# FileRelic session handoff

This file is the restart point for a new Codex/AI session or another project
contributor. Read it before changing code. It records the project owner's
context and learning needs, what the project is meant to become, what has
actually been implemented, and the safest next steps.

**Last updated:** 2026-11-03 (orphan scan added; docs reflect the current CLI and ext workflow)  
**Repository:** `/home/nikunj/FileRelic`  
**Project owner:** Nikunj, a student working on FileRelic with three other
group members. Nikunj is learning software engineering and low-level storage
concepts while the project is being built.

## Current application state (2026-11-03)

FileRelic is a read-only image inspector with a CLI and a Qt GUI over one shared
core library. It is **not yet** a recovery tool: no file inventory beyond the
ext root directory, no content reading, no extraction, no hashing, no case file.

Working:

- MBR, extended MBR/EBR (logical partitions numbered from 5), and GPT detection
  and validation, including GPT CRC checks.
- Probable-signature detection for NTFS, APFS, XFS, ext-family, Btrfs (a
  signature is evidence, not "support").
- An ext2/3/4 metadata reader: superblock; group descriptors (32- and 64-bit);
  inodes in any block group; ext4 extent trees including index levels; multi-block
  root directory. Validated against `debugfs` on two real `mke2fs` images
  (1 KiB and 4 KiB blocks): 135 entries each, 0 mismatches. Content is read
  via `ExtVolume::read_content`; inline-data files, symlink
  targets, and directory extraction are not supported. **Deleted-name listing** (`ExtVolume::list_deleted`, `ls --deleted`): finds names left in directory slack space and entries whose inode number was zeroed; each gets a `DeletedStatus` (name_only, inode_in_use, empty_file, block_map_cleared, blocks_reused, blocks_partly_reused, likely_intact, unknown). It never reads file content, so `likely_intact` only means the recorded blocks are free now. **Deleted-file recovery** (`ExtVolume::recover_deleted_content`, `extract --deleted`, `cat --deleted`, `--inode N`): path is folder + deleted name. Uses the deleted inode's extents, or when the filesystem erased them, the newest usable older inode copy from the journal (`find_journal_copies`). Every block is checked against the block bitmap; reused blocks are zero-filled (never copied, so another file's data cannot leak into a recovery) and the result is PARTIAL/`.partial`. Live inode numbers and erased inode numbers are refused. COMPLETE only means "every recorded block was read and was free"; it does not prove the bytes are the original. **Orphan scan** (`ExtVolume::list_orphans`, `ls --orphaned`): enumerates ext-family inodes whose `dtime` marks them deleted and that no live directory entry points at anymore. This is a read-only scan with honest caveats and no claim of content recovery. Fixture generator: `python3 tools/create_deleted_fixture.py [1024|4096]` builds `testdata/deleted-fixture-<bs>.raw` (git-ignored) with intact / journal-recovered / partly-reused / lost / empty cases and prints the original SHA-256 values; expected results are tabulated in `testdata/README.md`. Verified (Linux, scratch images, not committed): byte-identical recovery vs the original files for intact maps and for journal recovery (real `debugfs jo/jw` journals, with and without checksums), correct PARTIAL when blocks were overwritten, refusals, and ~1,300 mutated images under ASan+UBSan with no crash. Not covered: htree index slack, recursion into subdirectories, directory/symlink recovery, inline-data files, external journals, fast-commit areas, and real kernel-deleted images (no mount available when this was built).
- CLI: `inspect [-v] <image>` (`inspect-image` is a deprecated alias), `ls [-v] [-p N] [--deleted|--orphaned] <image> [path]`, `cat [-p N] <image> <path>`, `extract [-p N] <image> <path> <outdir>`, `demo`, `version`, `help`. Exit codes 0/1/2.
- File access inside ext images: path lookup, directory listing, file content for extent-mapped (ext4) and block-mapped (ext2/3, incl. double/triple indirect) files, sparse holes. `ls --orphaned` adds a read-only ext-family orphan scan: it enumerates inodes whose `dtime` indicates deletion and that are not reachable from any live directory entry. `extract` writes `<name>.partial`, renames when complete, never overwrites, reports status (complete/PARTIAL/FAILED) and SHA-256 of the output bytes. Safety: 1 GiB cap on zero-filled holes per file; overlapping extents rejected; refuses to write over the source image.
- Human-readable output (done): the core builds plain-language `ReportSection`s once
  (`report_format`, `report_sections`); the CLI prints them (`report_text.cpp`), the GUI
  draws them as severity-coloured cards. Technical rows hidden unless `-v` / GUI checkbox.
  Scanner warnings and `ls`/`cat`/`extract` output are in plain words too (human sizes,
  file/folder, Result/Size/SHA-256 lines; `ls -v` adds inodes).
- Qt Widgets GUI: section cards, "Show technical details" toggle, Advanced step-trace view,
  worker-thread inspection, `filerelic-gui <image>` to open an image directly.
- Synthetic fixtures in `testdata/` (generator `tools/create_practice_image.py`)
  and generated real ext4 images (`tools/create_real_ext4_fixture.py`, git-ignored,
  Linux + e2fsprogs) checked by `tools/verify_real_ext4.py`.

## Code organization

- `src/image_inspector.h`: public model (`InspectionReport`, `PartitionInfo`,
  `FileSystemInfo`, `DirectoryListing`, `DirectoryEntryInfo`, `InodeInfo`,
  `ExtentInfo`, `ReportSection`, `ReportRow`, `Severity`), `inspect_image()`, `render_report()`, `render_trace()`.
- `src/image_inspector.cpp`: runs the scan, then calls `build_sections`.
- `src/report_format.*`: shared plain-language wording helpers (sizes, type names).
- `src/report_sections.*`: builds the `ReportSection`s (the single source of wording).
- `src/report_text.cpp`: draws sections as CLI text.
- `src/image_source.*`: `ReadOnlySource` and `FileImageSource`. Bounds/overflow
  checked, 16 MiB per-read cap, no write method. All reads go through it.
- `src/partition_scanner.*`: MBR / EBR / GPT.
- `src/filesystem_probe.*`: signature detection (`detect_file_system`, five
  decisions, cyclomatic complexity 6, matching the course PDF).
- `src/ext_reader.*`: ext metadata reader.
- `src/byte_utils.h`, `src/scan_context.h`: small shared helpers.
- `src/cli_main.cpp`: argument handling and `demo`.
- `gui/`: Qt shell (`main_window.*`); `gui/CMakeLists.txt` is included by the root build.
- Root `CMakeLists.txt`: builds `filerelic_core`, `filerelic`, and `gui/filerelic-gui`
  when Qt 6 is found (`-DFILERELIC_BUILD_GUI=OFF` to skip).

`report.trace` is a step log for `--verbose` and the GUI Advanced view. Results
live only in the typed fields; nothing parses the trace.

## Build and check

```bash
cmake -S . -B build
cmake --build build
./build/filerelic demo
python3 tools/create_real_ext4_fixture.py 1024
python3 tools/verify_real_ext4.py build/filerelic testdata/real-ext4-1024.raw
```

Content verification: `python3 tools/verify_ext_content.py build/filerelic` (273 checks vs ground truth, `debugfs dump` and sha256) after generating the real ext4/ext2 fixtures; fuzz of inspect/ls/cat/extract under ASan+UBSan (600 further runs after the wording refactor, plus 1,134 earlier; no crash or timeout).

Verified when this was written (Linux only): clean `-Wall -Wextra` build; CLI
summaries identical to the pre-refactor version for all ten practice fixtures
except the intended logical-partition renaming and ext root-directory block;
~1,450 truncated/mutated images under ASan+UBSan without a crash; GUI builds,
launches headless and was screenshotted offscreen (ext4 and out-of-bounds MBR images). **Not verified:** clicking through the GUI (toggle), Windows/macOS
builds. There is no automated test suite.

## Recommended next sequence

1. **Deleted files on ext4:** names (`ls --deleted`) and recovery (`extract --deleted`, with journal fallback) are done. `ls --orphaned` is now added as the missing orphan-inode scan. A real kernel-style deleted-file fixture is generated via `tools/create_deleted_fixture.py`; it produces intact, journal-recovered, partially reused, and lost cases. The next ext work is recursive/directory extraction and inline-data handling.
2. **TSK research** (no teammate: do it in-session): `docs/TSK_RESEARCH_BRIEF.md`
   -> write `docs/TSK_FINDINGS.md` (version, license, per-filesystem capability,
   custom image backend, fixture commands) before NTFS work.
3. **Review the adapter contract** (`docs/ADAPTER_CONTRACT.md`, a draft) with the
   group and freeze it, then write the TSK adapter for ext4 and NTFS inventory.
   Keep third-party types out of the shared report, CLI, and GUI.
4. **Safe extraction and provenance:** write only to a user-chosen destination,
   collision-safe names, partial/failed status, SHA-256 of output (identifies
   bytes, does not prove completeness), then a versioned JSON case manifest
   before any PDF report.
5. **GUI and CLI workflows:** progress, cancel, candidate selection, destination,
   report export, only when the core can supply those states.
6. **Platform coverage:** build and package on Windows and macOS (only Linux has
   been built). Mark APFS encryption, XFS, Btrfs, snapshots, TRIM as limits.
7. **Regenerate diagrams** from the final code. The old class-diagram drafts were
   removed; `references/` keeps the original proposal and diagrams as source
   material (they describe XFS/Btrfs only; scope has since moved to ext4/NTFS first).

## How to work with Nikunj

Nikunj wants the AI to help build the application and teach him from the
beginning, not just deliver code. Assume he may not know the vocabulary yet.
Explain each important design choice in ordinary language, with a small
concrete example. Define terms such as partition, sector, inode, parser,
library, API, and control-flow graph before relying on them. Explain what a
change does, why the project needs it, how it connects to earlier work, and
what comes next.

Useful teaching approach:

1. Give a short progress note before doing repository work.
2. Inspect the current source and Git state before assuming history is current.
3. Make a meaningful, reviewable change rather than only giving Nikunj a list
   of commands to type.
4. At the end, summarize changed files, describe the design in beginner terms,
   mention any build/verification actually performed, and give the next
   concrete step.
5. Keep the work incremental enough that Nikunj can explain it to his
   professor and group. Avoid unexplained architectural leaps or dumping
   jargon.

Nikunj has already asked for an explanation of: command-line arguments
(`argc`/`argv`), `std::string_view`, namespaces, compile-time version macros,
raw disk images, sectors, MBR/GPT partition tables, ext superblocks, inode
numbers, metadata versus file content, and cyclomatic complexity. Treat those
as learning milestones already introduced; connect to them when useful rather
than assuming mastery.

The owner is comfortable running CMake and Git commands when given their
purpose. Explain the difference between configuring (`cmake -S ... -B ...`),
building (`cmake --build ...`), running the program, committing, and pushing.
Never imply a push or commit happened unless it actually did.

## Product goal

FileRelic is intended to become a cross-platform, read-only forensic disk-image
inspection and recovery application for Linux (including ext4), Windows
(including NTFS), and macOS (including APFS). The operating system running
FileRelic is separate from the filesystem inside the evidence image. First
support regular image files; direct physical-drive acquisition is later and
requires separate OS-specific work.

The intended end product has a CLI and a Qt desktop GUI over one shared core.
The core should discover partition layouts and filesystems, inventory files,
expose recovery candidates, extract selected data into a separate output
directory, compute hashes, retain case metadata, and export useful reports.
Support claims must reflect evidence and fixtures. Signature detection is not
the same as filesystem parsing, and parsing metadata is not the same as
recovering a deleted file.

The supplied UML diagrams in `references/diagrams/` are useful design hints,
not strict contracts. The owner explicitly authorizes changing, deleting, or
adding classes/functions to arrive at a better working design. Implement the
real application architecture first. Recreate/update the final class diagrams
after the project architecture is settled; do not distort code just to match
the old reference picture.

## Safety and quality boundaries

- Never mount or modify evidence images as part of ordinary inspection.
- Do not add physical-device writes. Acquisition must be separately designed
  and read-only by default.
- Never call a signature match “filesystem support” without qualification.
- Deleted-file recovery claims are limited to what the scratch-image evidence above shows (ext2/3/4 only); never claim a recovered file is the original, only that its recorded blocks were read.
- Preserve source paths, parser warnings, and limitations in results.
- Keep Windows/macOS path handling in mind (`std::filesystem::path` and Qt
  Unicode conversion); avoid Linux-only assumptions in shared code.
- Do not add or run tests unless Nikunj asks for tests/verification. A build
  may be needed to integrate code, but report exactly what was run.
- Do not commit, push, publish, or contact others unless explicitly asked.

## Git/workspace notes at handoff

The earlier session resolved a GitHub history merge involving an empty initial
README. The shared-core milestone was committed as `20d3d90` and Nikunj
confirmed it was pushed. Inspect the current branch and worktree before
discussing another commit or push. Keep the cyclomatic-complexity PDF/source
and `detect_file_system()` CFG PNG/source; those are requested course
deliverables, not obsolete class-diagram drafts.

## Short restart prompt

"Nikunj is learning C++/software engineering while building FileRelic; explain
each step plainly and never claim a commit, push, or test that did not happen.
Goal: a cross-platform read-only forensic recovery app, one core shared by CLI
and Qt GUI. Today: typed `InspectionReport`; partition scanning (MBR/EBR/GPT);
signature detection; an ext reader that decodes real ext4 extent trees and
matches `debugfs`; a `ReadOnlySource` abstraction; one root CMake build. Next:
read file content and hash it, finish the TSK research, freeze the adapter
contract, then extraction and a JSON case manifest. Read docs/SESSION_HANDOFF.md
and inspect Git state first."

## Working rule from the owner (2026-11-02)

Whenever code changes, update every affected `.md` in the same change (README, ARCHITECTURE, SESSION_HANDOFF, ROADMAP, DEVELOPMENT_PLAN, ADAPTER_CONTRACT, testdata/README, gui/README) and ship the changed files as an unzip-over-the-repo bundle.
