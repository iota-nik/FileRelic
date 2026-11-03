# FileRelic Roadmap

## Project outcome

FileRelic should end as an installable desktop application and CLI that analyze disk-image files without modifying them. It will identify partitions and supported file systems, present files and recovery candidates, extract selected content to a separate output directory, calculate hashes, and produce a case report.

The project should make careful claims. A file system can be supported for detection and inventory while deleted-file reconstruction is still incomplete. The UI, CLI, and report must show that distinction.

## Working project milestone

This is the minimum project that should be demonstrably useful:

- CLI and Qt desktop GUI use one shared inspection/recovery engine.
- Raw image input is read-only.
- MBR and GPT partitions are detected and validated.
- FileRelic detects and inventories supported file systems from controlled images.
- A user can extract supported file content to a separate output directory.
- Every output has an extraction status and SHA-256 hash.
- Case metadata and a JSON/HTML report are produced locally.
- The app builds and runs on Linux, Windows, and macOS; the same fixture corpus is used on every host where practical.

The first supported file systems for meaningful file extraction are ext4 and NTFS. APFS is then added with explicitly documented encryption/snapshot limits. XFS and Btrfs remain experimental until their fixture evidence supports stronger claims.

## Presentation milestone

The immediate goal is a credible, working prototype suitable for demonstration before the full project is complete. It must be honest about its current limits.

- `filerelic inspect` examines a raw image without modifying it.
- It recognizes and validates MBR, extended MBR/EBR chains, and GPT layouts.
- It lists valid partition boundaries and warns about malformed or out-of-bounds data.
- It identifies a small set of file-system signatures where they are present.
- It ships with generated safe practice images that demonstrate valid, invalid, extended, and GPT layouts.
- It has a short demonstration guide explaining the commands, expected output, and next planned stages.
- `ls --orphaned` now reports ext-family inodes that have `dtime` set and are no longer reachable from any live directory entry. This is an honest orphan scan, not a full deleted-file recovery engine.
- Recursive directory extraction is now supported for ext-family directories via `filerelic extract <image> <dir> <outdir>`, preserving the relative tree under the chosen output directory while keeping the same partial-write safety checks.
- Inline regular files and fast symlinks stored directly in the inode are now read as content; inline directories remain an unsupported traversal edge case.

This is a prototype of a forensic recovery tool, not yet a complete recovery suite. It must not claim that it recovers deleted files until extraction code and controlled-fixture evidence exist.

## Phases

| Phase | Status / result | Learning focus | Agent use |
| --- | --- | --- | --- |
| 1 — Image and MBR foundation | **Done:** bounded read-only image inspection, MBR/EBR validation, safe fixtures | byte offsets, sectors, partition bounds | Build together; no specialist needed |
| 2 — GPT foundation | **Done:** protective MBR, primary/backup headers, CRC checks, partition array and fixture | checksums, redundant metadata, GUIDs | Build together; no specialist needed |
| 3 — Shared inspection core | **Current baseline:** CMake core library; CLI and GUI call it; `InspectionReport` has typed table/partition/filesystem fields | library/API boundaries, value models, UI rendering | Keep one integrator while contracts are small |
| 4 — Finish structured inspection | **Done 2026-11-02:** typed ext directory/inode/extent records, trace retired from default output, one root build for CLI + optional GUI | returning data rather than printing, model-driven rendering | Nikunj + primary coding agent; group members can review the report model and fixtures |
| 5 — Real filesystem inventory | **Current status:** NTFS remains a contract-and-signature layer, not a full inventory engine; TSK review is documented and the adapter boundary is explicit | third-party parser boundary, allocated/deleted metadata versus recovery | One member researches parser/license/feature evidence while core API stabilizes |
| 6 — Safe recovery and case data | **Current slice:** destination-only extraction, collision handling, SHA-256, statuses, versioned JSON manifest export via `filerelic report` and the GUI export flow | streaming I/O, integrity limits, provenance | A small shared report-manifest layer is now in place; the next step is broader job orchestration |
| 7 — Recovery GUI and CLI workflows | **Current slice:** GUI browses ext partitions, selects live/deleted-name candidates, extracts them, and automatically updates an investigation manifest with outcomes and hashes; responsive two-pane layout and asynchronous browsing/work are in place | asynchronous jobs, events, UI state | Progress is indeterminate; cooperative cancellation and GUI interaction tests remain |
| 8 — Additional filesystem coverage | APFS with encryption/snapshot limits; XFS/Btrfs capability-gated and experimental | filesystem-specific evidence and support claims | Independent adapter/fixture work can run in separate directories |
| 9 — Cross-platform packaging | Build/install on Linux, Windows, and macOS; fixture matrix and release artifacts | CMake packaging, dependencies, platform testing | Packaging owner plus platform reviewers |

## Suggested calendar

For a student project worked on several focused sessions each week, plan roughly **10–16 weeks** for the working milestone. The largest uncertainty is real file-system parsing and cross-platform packaging, not the command-line or GUI work.

Do not treat calendar time as a substitute for acceptance evidence. A phase is complete only when its controlled fixtures, builds, and documented limitations pass review.

## When agents join

Agents are helpers with narrow assignments; they are not a replacement for the main build-and-learn loop.

1. **Now:** finish the structured report and core boundaries with the primary coding agent; avoid parallel edits to shared models.
2. **In parallel now:** one group member can research TSK/alternatives, current filesystem coverage, license terms, and suitable controlled fixtures. Return a short recommendation; do not modify the shared API yet.
3. **After the adapter contract is agreed:** split adapter implementation and cross-platform CMake/CI across separate directories, with one integrator reviewing and merging.
4. **During filesystem expansion:** assign APFS and XFS/Btrfs evidence/fixtures independently; keep unsupported features labeled clearly.
5. **Before release:** assign packaging, fixture regression, and documentation reviews; the integrator owns end-to-end integration.

Never run several agents against the same shared files. Shared models are changed first, then each implementation branch consumes them, then the integrator runs end-to-end checks.

## Immediate next steps (updated 2026-11-03)

The ext4 reader and its recovery evidence, the TSK research, the manifest export,
and the first usable GUI browse/extract workflow are in place. The GUI automatically
saves a JSON investigation manifest after inspection and appends extraction
outcomes/hashes to it. Next priorities: implement a real NTFS inventory adapter
against the documented contract, broaden focused automated tests, and validate
Windows/macOS builds. Add safe cooperative cancellation and a PDF renderer only
after their shared core/report contracts are ready. See [SESSION_HANDOFF.md](SESSION_HANDOFF.md).
