# FileRelic session handoff

This file is the restart point for a new Codex/AI session or another project
contributor. Read it before changing code. It records the project owner's
context and learning needs, what the project is meant to become, what has
actually been implemented, and the safest next steps.

**Last updated:** 2026-10-28  
**Repository:** `/home/nikunj/FileRelic`  
**Project owner:** Nikunj, a student working on FileRelic with three other
group members. Nikunj is learning software engineering and low-level storage
concepts while the project is being built.

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

## Current application state

The repository started with an image-inspection prototype. It currently
contains:

- C++17 and CMake build setup.
- A CLI with `--help`, `--version`, `inspect` / `inspect-image`, and `demo`.
- Read-only regular-image inspection.
- MBR, extended MBR/EBR, and GPT detection/validation, including GPT CRC checks.
- Probable signatures for NTFS, APFS, XFS, ext-family, and Btrfs.
- A limited ext-family metadata demonstration: selected superblock fields,
  root-directory entries, and partial inode/block interpretation on controlled
  synthetic fixtures. This is not full ext4 support and does not recover files.
- Synthetic practice images under `testdata/` and a generator at
  `tools/create_practice_image.py`.
- A Qt Widgets GUI with readable result cards and an Advanced diagnostic log.

The GUI used to launch the CLI using `QProcess`, then heuristically split its
terminal text into cards. That bridge has been removed: both executables now
compile the shared inspection implementation, and the GUI invokes
`filerelic::inspect_image()` directly with Qt Concurrent so it does not block
the window. `InspectionReport` now carries typed partition table state,
partition records (scheme, name/type, start, length, bounds validity),
filesystem identification and selected ext metadata, warnings, diagnostics,
and presentation sections. The parser fills these records while it inspects
the image; it no longer reparses text to discover them. The scanner writes its
compatible text trace to a per-inspection stream, not process-wide `std::cout`
or `std::cerr`.

## Latest code organization

- `src/image_inspector.h`: public `InspectionReport`, `PartitionInfo`,
  `FileSystemInfo`, `ReportSection`, `inspect_image()`, and `render_report()`.
- `src/image_inspector.cpp`: inspection engine implementation; parser functions
  fill typed partition/filesystem records and write the compatibility trace to
  a local stream.
- `src/cli_main.cpp`: command-line argument handling and demo command.
- `CMakeLists.txt`: builds `filerelic_core` and links the CLI to it.
- `gui/main_window.*`: Qt UI; now calls shared API through Qt Concurrent.
- `gui/CMakeLists.txt`: builds a GUI-local static `filerelic_core` from the
  same source and links Qt Widgets + Concurrent.
- `docs/ARCHITECTURE.md`, `gui/README.md`, and root `README.md`: updated to
  explain the direct core-library relationship.

The GUI is currently configured separately from the root build. Root CMake
builds the CLI/core; the GUI is configured with `cmake -S gui -B build-gui`.
Qt is an external dependency and is not bundled. Qt 6.2-compatible CMake
automoc conventions are used.

## Immediate engineering task

Continue from the typed shared-report milestone. The latest structural change
has been compiled successfully in the current workspace (no test suite was
added or run). To reproduce the build, use:

```bash
cmake -S . -B build
cmake --build build
cmake -S gui -B build-gui
cmake --build build-gui
```

Fix compile errors in the current code, then manually run a safe demo or a
synthetic image only if needed to confirm the CLI and GUI link to the new core.
Do not write to user disk images. There is no need to commit or push unless
Nikunj asks.

Explain to Nikunj that a *library* is compiled code that multiple programs can
call: the CLI and GUI now share the same inspection function. `InspectionReport`
is the shared result object. It carries actual partition and filesystem
records; the GUI renders them without decoding CLI text. The old text report
remains for the CLI and Advanced log during migration. Qt Concurrent runs
inspection on a worker thread so the interface remains responsive.

## Recommended next sequence

Keep this order unless repository evidence or a concrete blocker changes it:

### 1. Stabilize shared-core linking

Build root CLI and GUI. Confirm no code still expects `QProcess` or
`FILERELIC_CLI_PATH`. Ensure command help/demo and GUI still work. The
implementation currently duplicates the core target definition in standalone
GUI CMake; consider a single root CMake option/build that can add the GUI when
Qt is present, but do that only after the direct-link step is sound.

### 2. Render both interfaces from typed data

The GUI uses typed records, but the CLI still prints the compatibility trace
stored in `InspectionReport::rendered_text`. Add a text renderer over the typed
fields and preserve legacy details where the report already contains them.
Then retire parser stream output after the CLI and GUI both render the same
data model.

### 3. Complete the typed inspection results

The current types cover basic table/partition fields and selected ext metadata.
Add the remaining GPT validation details, EBR warnings, filesystem confidence
and capability status, ext directory/inode findings, and provenance as parser
work proceeds. Keep rendering separate: CLI renders text/JSON; GUI builds its
own cards and tables from the same fields. Keep unknown values explicit rather
than using fake zeroes.

### 4. Split core responsibilities into focused files/classes

Move low-level bounded image reads into a read-only image source abstraction;
separate partition-table discovery (MBR/EBR/GPT), filesystem detection, and
filesystem-specific metadata adapters. Start with the smallest useful
seams—not a forest of empty classes. Give each type a clear input/output and
make dependencies point inward toward core models. Preserve read-only source
access.

### 5. Add a parser adapter and select real recovery support

Research and integrate The Sleuth Kit (TSK) or another well-supported parser
behind a FileRelic-owned adapter interface. Keep third-party types out of the
GUI and shared report. Start by enumerating metadata from controlled ext4 and
NTFS images; track parser support, deleted-entry visibility, extents, and
recovery separately. Check current versions and license terms before pinning
dependencies because those details can change.

### 6. Add safe extraction and provenance

Write recovered output only to a user-selected destination, with collision-safe
names and partial/failure status. Hash output with SHA-256, but explain that a
hash identifies output bytes and does not prove that reconstruction is
complete. Keep source image read-only. Add a versioned JSON case manifest
before PDF reporting.

### 7. Connect useful GUI and CLI workflows

Use shared application services. Add progress, cancel, candidate selection,
output destination, and reports only when the core can supply those states.
CLI remains useful for automation and reproducible demos; GUI should provide a
clear visual workflow rather than merely showing the same terminal transcript.

### 8. Platform and filesystem coverage

Build and package on Linux, Windows, and macOS. The environment used so far is
Ubuntu/Linux. A successful Linux build does not prove Windows/macOS portability.
Use the same safe fixtures where possible and clearly mark APFS encryption,
XFS, Btrfs, snapshots, TRIM, and unsupported filesystem features.

### 9. Regenerate diagrams after implementation settles

The current PlantUML diagrams are historical snapshots of the earlier
reference/current distinction. Since Nikunj now wants diagrams redone after
the project architecture is built, prioritize correct code and this handoff
document first. Then generate diagrams from the implemented architecture and
update README links together.

## Safety and quality boundaries

- Never mount or modify evidence images as part of ordinary inspection.
- Do not add physical-device writes. Acquisition must be separately designed
  and read-only by default.
- Never call a signature match “filesystem support” without qualification.
- Do not claim deleted-file recovery until controlled fixtures demonstrate it.
- Preserve source paths, parser warnings, and limitations in results.
- Keep Windows/macOS path handling in mind (`std::filesystem::path` and Qt
  Unicode conversion); avoid Linux-only assumptions in shared code.
- Do not add or run tests unless Nikunj asks for tests/verification. A build
  may be needed to integrate code, but report exactly what was run.
- Do not commit, push, publish, or contact others unless explicitly asked.

## Git/workspace notes at handoff

The prior session had user-created commits and a GitHub history merge involving
an empty initial README. Do not assume the remote or current branch state.
Inspect with `git status --short`, `git log --oneline -5`, and `git remote -v`
before discussing commits or pushes. The present working tree contains
uncommitted diagram-related edits and CFG artifacts from earlier work; preserve
them unless Nikunj asks to clean them. In particular, do not delete the
cyclomatic-complexity graph or its source when reorganizing class diagrams.

## Short restart prompt

If context is nearly exhausted, resume with this summary: “Nikunj is learning
C++/software engineering while building FileRelic; explain each step plainly.
The goal is a real cross-platform read-only forensic recovery app with one core
shared by CLI and Qt GUI. The latest change moves the inspector into
`filerelic_core`; `InspectionReport` now carries typed partition and filesystem
records, and the GUI renders them via Qt Concurrent. Build both targets after
the latest changes. Next render CLI output from the same fields, complete
validation/warning records, separate source/partition/filesystem adapters,
add real metadata
enumeration, safe extraction, hashes, case reporting, and platform packaging.
The supplied UML diagrams are guidance only; Nikunj wants final diagrams after
the architecture settles. Read this handoff and inspect Git state before work.”
