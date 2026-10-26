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

- `filerelic inspect-image` examines a raw image without modifying it.
- It recognizes and validates MBR, extended MBR/EBR chains, and GPT layouts.
- It lists valid partition boundaries and warns about malformed or out-of-bounds data.
- It identifies a small set of file-system signatures where they are present.
- It ships with generated safe practice images that demonstrate valid, invalid, extended, and GPT layouts.
- It has a short demonstration guide explaining the commands, expected output, and next planned stages.

This is a prototype of a forensic recovery tool, not yet a complete recovery suite. It must not claim that it recovers deleted files until extraction code and controlled-fixture evidence exist.

## Phases

| Phase | Main result | Learning focus | Agent use |
| --- | --- | --- | --- |
| 1 — Current | Raw image inspector, MBR validation, extended partitions | sectors, offsets, MBR, EBRs, safe binary reads | Work together; no specialist needed |
| 2 — Next | GPT fixture and GPT header/partition-array validation | checksums, redundant metadata, GUIDs, modern partitioning | Work together; GPT is part of the foundation |
| 3 | Core inspection library and structured results | header/source separation, data models, errors, tests | One bounded agent may audit TSK/API options while we build the core |
| 4 | First real file-system adapter: ext4 and NTFS | file-system metadata, library integration, fixtures | Use one parser-research agent for independent validation and one build agent only if needed |
| 5 | Safe extraction, hashes, case manifest, reports | streaming I/O, partial results, SQLite/JSON | One independent agent can review output safety and report schema |
| 6 | Qt desktop GUI | shared engine, UI state, background scan jobs | Build together; use an agent for isolated UI research or accessibility review |
| 7 | APFS, XFS, Btrfs capability matrix and adapters | feature limits, cross-validation, forensic claims | This is the best stage for filesystem-specialist agents working on separate adapters |
| 8 | Packaging, cross-platform tests, release candidate | installers, dependency licenses, test matrix | Use a build/packaging agent plus parser agents for platform failures |

## Suggested calendar

For a student project worked on several focused sessions each week, plan roughly **10–16 weeks** for the working milestone. The largest uncertainty is real file-system parsing and cross-platform packaging, not the command-line or GUI work.

Do not treat calendar time as a substitute for acceptance evidence. A phase is complete only when its controlled fixtures, builds, and documented limitations pass review.

## When agents join

Agents are helpers with narrow assignments; they are not a replacement for the main build-and-learn loop.

1. **Now through GPT:** work directly with the primary agent. The code and concepts are small enough that parallel work would add coordination overhead.
2. **After GPT and the core API are stable:** one research agent can independently inspect TSK support, licensing, and test-fixture requirements. Its deliverable is a written recommendation, not a direct change to shared code.
3. **After the shared adapter interface is frozen:** use up to two agents in parallel on separate directories, such as a file-system adapter and cross-platform build setup.
4. **During multi-file-system work:** add dedicated XFS/Btrfs and APFS validation agents because those investigations are genuinely independent.
5. **Before release:** use agents for packaging, fixture regression, and documentation review; the integrator remains responsible for merging and verifying changes.

Never run several agents against the same shared files. Shared models are changed first, then each implementation branch consumes them, then the integrator runs end-to-end checks.

## Immediate next step

Build GPT support from a synthetic image: generate a valid protective MBR, primary GPT header, backup GPT header, and a small partition-entry array. FileRelic will validate both headers and list the partitions. This is the last partition-table foundation before starting the shared core library and real file-system adapters.
