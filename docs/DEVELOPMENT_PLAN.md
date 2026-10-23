# FileRelic: Cross-Platform Forensic Recovery Toolkit

## Purpose

Build a desktop application and CLI for examining disk images and recovering files from common file systems. It must install and run on Windows, macOS, and Linux. The first release focuses on read-only image analysis, verifiable results, and a repeatable test corpus. Direct physical-drive acquisition is a later milestone.

This plan is intended to be split across multiple developers or coding agents. Keep interfaces stable, assign one owner per workstream, and integrate through the contracts below.

## Project-owner learning path (build from 0 to 100)

The project owner is learning the workflow while building. Keep the work incremental: explain the idea and the next command before using it, make one small change at a time, then review what changed and what the result means. Use agents to help with bounded tasks, not to hide the code or decisions from the owner.

1. **0–10: Workspace and Git.** Learn what a repository is, how files/commits/branches relate, and how to inspect a change before keeping it. Keep the FileRelic source repository distinct from its reference documents.
2. **10–20: Build a tiny C++ program.** Learn source files, headers, compiler errors, and the difference between building and running. Make a `filerelic --version` command.
3. **20–30: CMake and Qt shell.** Learn that CMake configures the build while the compiler builds it. Create the same tiny program as a Qt Widgets desktop window; keep the CLI target too.
4. **30–40: Raw images and safe reads.** Learn what a disk image is, offsets/bytes, bounds checks, and why the source stays read-only. Implement `inspect-image` for a small generated raw image.
5. **40–50: Partitions and fake adapter.** Learn MBR/GPT partition tables and an adapter boundary using a fake file system. Connect source → job → CLI output before adding real parser complexity.
6. **50–60: First real parser.** Integrate TSK for one tractable fixture and first enumerate files/metadata. Learn the difference between parser support, deleted metadata discovery, and actually reconstructing file bytes.
7. **60–70: Recovery, status, and hashes.** Add extraction for controlled fixtures. Learn fragmented extents, partial recovery, SHA-256, and why a calculated output hash alone does not prove completeness.
8. **70–80: Case record and GUI.** Add the versioned manifest/SQLite case data and connect Qt to the same scan service. Learn models, events, asynchronous jobs, cancellation, and UI state.
9. **80–90: File-system coverage.** Validate NTFS, APFS, and ext4 independently; investigate XFS/Btrfs as experimental. Learn fixture ground truth, parser limitations, and cross-validation.
10. **90–100: Packaging and release.** Build/install on Windows, macOS, and Linux; learn dependency pins, licenses, signing, and release evidence. Add E01 only when the raw-image pipeline is stable.

At every milestone the owner should be able to explain: what input enters the component, what output it produces, what can fail, and how we know it worked. The primary agent should explain diffs in plain language and ask the owner to choose between genuine product tradeoffs; it should not dump a large codebase or launch all specialist agents by default.

## Product scope

### First-release host platforms

- Windows 10/11 x64 (and ARM64 if dependencies and packaging permit).
- macOS on Apple Silicon and Intel where supported by Qt and dependencies.
- Linux x86_64, with AppImage and/or distro packages as the initial distribution formats.

The OS running the app is independent of the file system in the evidence image. For example, a Linux build should analyze an NTFS image, and a Windows build should analyze an ext4 image.

### File-system scope

| Priority | File systems | Release intent |
| --- | --- | --- |
| Core | NTFS, APFS, ext4 | Supported through a common parser interface and test fixtures |
| Experimental | XFS, Btrfs | Ship only with explicit experimental labels and known-limitations reporting |
| Follow-on | exFAT, FAT, HFS+ | Add after core scan, verification, and report flows work |
| Out of first release | ReFS | Separate feasibility and parser-support investigation |

APFS encryption must be reported as a constraint: parsing encrypted data requires appropriate access/keys. Deleted-file recovery is not guaranteed, especially where snapshots, TRIM, SSD behavior, or overwritten blocks affect what remains available. Recovery claims must be based on observed evidence and test results, not promises of complete recovery.

## Architecture decision

Use **C++17 and Qt 6 Widgets** for a single cross-platform application with both GUI and CLI entry points. Use **CMake** for builds. Start with C++17, which meets Qt 6's modern baseline and reduces compiler/toolchain friction; move to C++20 only if a concrete feature justifies it across all supported build environments. Use The Sleuth Kit (TSK) as the first general-purpose parser, isolated behind our own adapter API. Treat each format as an independently tested capability: TSK support is not equivalent to proven deleted-file recovery. XFS and Btrfs remain experimental. Do not let UI or reporting code depend directly on TSK types.

Use Qt Widgets rather than Qt Quick/QML for the first version: this is a data-heavy desktop utility with forms, tables, progress, and preview panes, and Widgets keeps the deployment surface smaller. Keep core/job code independent from Qt UI types so a CLI and future alternate UI remain possible. Build adapters as statically registered modules at first; introduce runtime-loaded plugins only if external parser extensions become a real requirement.

```text
┌──────────────────────────┐
│ Qt desktop UI            │      CLI
└────────────┬─────────────┘       │
             └──────────┬──────────┘
                        ▼
             Application / Job API
                        ▼
        Case store + scan orchestration
          SQLite metadata / file outputs
                        ▼
          Read-only source abstraction
           disk image first; devices later
                        ▼
                Parser adapter API
            ┌───────────┼───────────┐
            ▼           ▼           ▼
           TSK      XFS adapter  Btrfs adapter
            └───────────┼───────────┘
                        ▼
       Recovery candidates + provenance
                        ▼
          Verification, manifest, report
```

### Component responsibilities

1. **Application layer:** case lifecycle, scan jobs, cancellation, progress, configuration, and coordination. It must not contain file-system parsing logic.
2. **Source abstraction:** seekable, bounded, read-only byte access over an image file. Enforce range checks and return explicit I/O errors. Disk images are the initial input. Add physical devices only through a separate OS-specific acquisition module that defaults to read-only.
3. **Parser adapters:** identify partitions and file systems, enumerate metadata and deleted entries, read file extents, and return parser warnings/limitations. Adapters must not write to the source.
4. **Recovery/output:** write recovered content only to a user-selected output directory, with collision-safe names. Never write into the source image or source device.
5. **Verification:** compute SHA-256 for the source image and recovered files; record byte counts, extraction status, and any parser-provided checksum result separately. A recovered-file hash proves the identity of the output bytes, not that those bytes are complete or authentic.
6. **Case/reporting:** SQLite for case and job metadata; JSON manifest as the stable machine-readable export; HTML follows once JSON is stable; PDF is a later milestone.
7. **UI/CLI:** consume the same application/job API. The CLI is required for reproducible automation and CI fixtures.

## Stable integration contracts

Agree on these contracts before parallel implementation begins. Keep them in a small shared `core` module and review any changes with all workstream owners.

### Source interface

```cpp
struct ReadResult {
    std::vector<std::byte> bytes;
    std::optional<std::string> error;
};

class ReadOnlySource {
public:
    virtual ~ReadOnlySource() = default;
    virtual uint64_t size() const = 0;
    virtual ReadResult read(uint64_t offset, uint64_t length) = 0;
    virtual std::string description() const = 0;
};
```

An implementation must reject overflow and out-of-range reads. Image source objects must not expose write methods.

### Parser adapter interface

Each adapter reports a stable ID/version, supported features, detected volume information, findings, and limitations. Findings should use shared structures rather than native parser objects. Minimum concepts: partition/volume, file entry, path/name, allocated/deleted/unknown state, timestamps with source and precision, logical size, extents, recovery status, warnings, and provenance.

Unknown values remain unknown; do not substitute zero or fabricate timestamps. Preserve raw parser timestamps/flags in provenance when practical.

### Job and result interface

Jobs emit typed events: started, phase/progress, finding, warning, recovered item, completed, failed, and cancelled. Events must be safe for CLI JSON-lines output and UI display. Cancellation is cooperative and must not corrupt the case database or leave partial files presented as complete.

### Case manifest

Version the JSON schema from its first commit. Minimum fields:

- schema version, app version, job ID, start/end timestamps in UTC;
- source kind, source size, source SHA-256, and image format if detected;
- OS/build information and parser adapter IDs/versions;
- detected partition/file-system details;
- scan settings, counts, warnings, and errors;
- each recovered item’s stable ID, source metadata reference, output path, byte count, SHA-256, status, and limitations.

Never store passwords or encryption keys in the manifest or logs.

### Contract ownership (single source of truth)

| Contract / shared file | Owner | Consumers | Change rule |
| --- | --- | --- | --- |
| C++ models, errors, source/adapter interfaces, scan events | Agent A | B, C, D, E, F | Only A edits after contract freeze; any change requires lead approval and a migration note |
| Image-source implementation and partition records | Agent B | C, D, A | B owns implementation; shared record shape remains owned by A |
| Adapter capability IDs and TSK-to-common mapping | Agent C | A, E, F | C owns TSK internals only; common result types stay owned by A |
| XFS/Btrfs feature declarations and evidence notes | Agent D | A, E, F | D proposes capability changes; A owns the common schema/type change |
| Job event presentation and CLI JSON-lines rendering | Agent E | users, CI | E serializes A's event model; do not create a second event/state model |
| SQLite migrations and exported manifest schema | Agent F | E, G, integrator | F owns schema/migrations; version and document breaking changes before coding |
| Root build, dependency pinning, CI, release packaging | Agent G | all streams | G alone edits root CMake/CI/release configuration; streams provide adapter-local CMake fragments and dependency requirements |
| Cross-stream decisions, merge order, support claims | Agent 0 (integrator) | all | Lead records decisions; no stream may silently change a frozen contract or support label |

No stream may copy shared data structures into its private module. Each parser maps into A's common structures; E renders A's events; F persists the agreed records; G links the agreed modules. This one-way dependency direction prevents circular includes and mismatched duplicate models.

## Suggested repository layout

```text
filerelic/
  CMakeLists.txt
  apps/gui/                 # Qt UI; UI owner
  apps/cli/                 # headless interface; core/API owner
  core/                     # shared models, jobs, interfaces
  source/                   # image reader, later OS device access
  adapters/tsk/             # TSK wrapper
  adapters/xfs/             # experimental specialization if needed
  adapters/btrfs/           # experimental specialization if needed
  case/                     # SQLite and manifest schema
  reporting/                # JSON first; PDF/HTML follow-on
  tests/                     # unit/integration tests and fixture metadata
  testdata/                  # generated or legally redistributable fixtures
  packaging/                 # platform packaging and signing configuration
  docs/                      # architecture, threat model, user guide
```

## Parallel workstreams

Assign each stream to a separate agent/owner. Each owner should work on its named directory and avoid changing shared interfaces without agreement. Integrate via small pull requests or isolated branches; do not have multiple agents edit the same files simultaneously.

### A. Core contracts and job orchestration

**Owns:** `core/`, shared models, error types, job event contract, adapter interfaces.

**Deliver:** compilable core library, cancellation/progress model, versioned result structures, small fake adapter for integration. Keep UI and TSK details out of the public API.

**Depends on:** architecture contract approval by integrator.

### B. Image sources and partition discovery

**Owns:** `source/`, image reader, partition discovery integration, input validation.

**Deliver:** bounded read-only image interface; partition table detection; clear errors for unsupported/invalid images; tests for short reads, out-of-range reads, and malformed input. Start with raw images. Add E01/VHD/VMDK only when chosen libraries and licenses are reviewed.

**Must not:** open physical devices for write or implement UI.

### C. TSK adapter and file-system coverage

**Owns:** `adapters/tsk/`, TSK adapter-local build metadata, and TSK version/feature validation. Agent G owns root dependency pins and CI integration. Use TSK 4.15.0 as the research baseline, but pin the version actually validated by CI rather than silently tracking latest.

**Deliver:** wrapper for NTFS, APFS, ext4 and TSK-supported formats behind the shared adapter API; report parser limitations faithfully; provide dependency/license inventory and sample fixture results.

**Must not:** expose TSK headers/types outside the adapter or imply deleted-file recovery works equally across formats.

### D. XFS/Btrfs feasibility and adapters

**Owns:** `adapters/xfs/`, `adapters/btrfs/`, filesystem-specific fixture design, support matrix.

**Deliver:** determine what supported TSK versions actually parse; produce minimal read-only adapter(s) or a documented gap analysis; test metadata listing and extraction against controlled images; record edge cases and unsupported features. Keep both marked experimental until acceptance criteria pass.

**Must not:** duplicate general orchestration or alter other adapter contracts without coordination.

### E. GUI and CLI experience

**Owns:** `apps/gui/`, `apps/cli/` (coordinate CLI event/schema with core owner).

**Deliver:** create/open case, select image and output directory, launch/cancel scan, show progress/findings/warnings, review recovery candidates, export report. CLI must run a scan from arguments and emit stable JSON-lines events/results.

**Must not:** parse file systems or implement hashing itself; call shared services.

### F. Case store, verification, and reporting

**Owns:** `case/`, `reporting/`, manifest schema, output writer and hashing service.

**Deliver:** SQLite migrations, schema-versioned JSON manifest, SHA-256 service, collision-safe output naming, JSON export, then HTML report. PDF is a follow-on. Include interrupted/failed extraction states.

**Must not:** mark a file “verified” solely because hashing succeeded; distinguish hash computation from source-checksum validation.

### G. Build, packaging, and CI

**Owns:** `packaging/`, CI configuration, developer build documentation.

**Deliver:** reproducible CMake builds and test runs on Windows, macOS, and Linux; platform package prototypes; dependency/license inventory; release checklist for signing/notarization. Use native runners for platform-specific package creation where required.

**Must not:** silently download unpinned dependencies in release builds.

## Detailed agent distribution and handoff briefs

The plan defines **8 responsibility areas**: one integrator (Agent 0) and seven specialist workstreams (Agents A–G). These are not eight agents that must be started together. The recommended project staffing is one primary coding/integration agent (Agent 0, with the project owner learning and reviewing) at first. Bring in one specialist agent for a bounded task when needed. After the contracts and CI are working, use at most two or three simultaneous agents on independent directories. Only grow toward all seven specialists if the project, schedule, and review capacity justify that overhead.

The local Git repository now contains a minimal C++17 CLI scaffold (`filerelic --help` / `filerelic --version`) and builds with CMake on the current Linux machine. It does not yet have a remote repository, shared scan API, Qt UI, or parser integration. A beginner-friendly next phase is: learn and review this scaffold; add a small `inspect-image` vertical slice; define only the API needed for a fake image scan; then ask Agent G to validate the build matrix. Do not start parser, GUI, database, and filesystem-specialist coding until that slice works. Other specialists can begin with short research or fixture proposals that do not modify shared code.

| Agent | Primary responsibility | Owns | Main upstream dependency |
| --- | --- | --- | --- |
| 0 | Integration, architecture, decision log, cross-stream review | root policy/docs and merge decisions | none; freezes shared contracts |
| A | Common C++ API, models, errors, scan jobs/events, fake adapter | `core/` and shared headers | Agent 0 contract approval |
| B | Read-only raw image source and partition discovery | `source/`, implementation in `volume/` | Agent A source and volume model |
| C | TSK backend and NTFS/APFS/ext4 capability validation | `adapters/tsk/` | A adapter contract; B image source |
| D | XFS/Btrfs feasibility, adapter work, feature limits | `adapters/xfs/`, `adapters/btrfs/`, filesystem notes | A adapter contract; B image source |
| E | Qt Widgets UI and CLI | `apps/gui/`, `apps/cli/` | A job/event API; F manifest schema |
| F | SQLite, manifest, safe extraction output, hashing, reports | `case/`, `reporting/` | A common result model; E integration needs |
| G | Root CMake, dependency pins, CI, installers and release workflow | root build/CI and `packaging/` | initial scaffold and stream dependency declarations |

The lead owns repository-wide decisions, shared contracts, merge order, acceptance review, and conflict resolution; the lead should not rewrite stream-owned implementations while an owner is active. When there are fewer contributors, combine adjacent responsibilities after the shared contract is agreed: A+B, E+F, or C+D. Keep packaging/CI ownership with the integrator or a focused G assignment. Parallel agents are useful only when their tasks are independent and their file ownership is clear; dependent steps and shared-file edits should stay with the integrator. Each agent has its own work context, but agents sharing a workspace can still collide on shared files, so the ownership table and merge gates remain mandatory.

### Integrator / technical lead (Agent 0)

**Mission:** keep a working, secure, reviewable product moving while parallel work lands.

**First tasks:**

1. Create the repository skeleton and branch/PR rules. Add `CONTRIBUTING.md`, `docs/ARCHITECTURE.md`, decision log, and a small buildable CMake target.
2. Convert this plan into issues with dependencies and named owners. Label each issue `contract`, `core`, `parser`, `ui`, `qa`, or `release`.
3. Hold a contract review with Agents A–G. Freeze the source, volume, adapter capability, event, and manifest contracts before implementation fans out. Each specialist returns a written acknowledgment or a concrete change request.
4. Create a fake parser adapter and a tiny generated raw image as the integration fixture. Use these to validate the app/job/event/result plumbing before real parsers are connected.
5. Require each PR to state changed interfaces, tests/fixtures used, platform tested, limitations, and license/dependency changes. Reject unapproved edits to files owned by another stream.

**Ongoing responsibilities:**

- Keep `main` buildable and ensure one feature branch per workstream.
- Review cross-cutting changes to schema, error taxonomy, IDs, naming, timestamp semantics, and cancellation behavior.
- Ensure user-visible support labels match the fixture-based support matrix.
- Integrate features in the order in the integration plan; reject undocumented claims of successful recovery.
- Maintain decisions and open risks in `docs/DECISIONS.md` and `docs/KNOWN_LIMITATIONS.md`.

**Done when:** a clean checkout builds; fake adapter can scan a fixture through CLI and UI; every integration milestone has an owner and reproducible evidence; no agent's private interface assumptions are left undocumented.

### Agent A — Core models and job orchestration

**Owns:** `core/`, shared public headers, job/event model, result/error types.

**Implement in order:**

1. `Error` type with category, stable code, human message, recoverability, and optional detail. Avoid leaking platform-specific/parser exception types.
2. Shared types for case ID, job ID, source description, partition/volume, file entry, extents, timestamps, findings, warnings, recovery state, and provenance.
3. `ReadOnlySource` and `FileSystemAdapter` abstract contracts from this document. Keep ownership/lifetimes explicit; do not expose raw parser pointers.
4. `ScanRequest` and `ScanEvent` variants for phases, progress, finding, warning, recovery result, cancellation, and terminal status.
5. `ScanService` orchestration with cooperative cancellation, event ordering, and a fake adapter for integration.
6. Unit tests for state transitions, duplicate events, invalid requests, cancellation, unknown timestamps, and error conversion.

**Handoff to:** B supplies sources; C/D supply adapters; E consumes `ScanService`; F consumes stable results. Publish public headers and a short example before other streams bind to them.

**Acceptance:** types compile with MSVC, Clang, and GCC; adapter can be injected without UI/TSK dependencies; terminal state is emitted exactly once; cancellation cannot be reported as complete; unknown/low-confidence values remain explicit.

### Agent B — Read-only image input and volume discovery

**Owns:** `source/`, `volume/` (if separate), image fixtures utilities related to source reading.

**Implement in order:**

1. `FileImageSource` with random-access reads, file size, path normalization, short-read reporting, bounded reads, and no write capability.
2. Overflow-safe offset/length checks and maximum request limits to prevent malformed parser inputs from causing unsafe allocations.
3. Partition discovery for GPT and MBR through an agreed parser/dependency; return partition byte offsets/lengths and warnings for damaged/overlapping entries.
4. Raw image selection and validation from CLI. Do not follow symlinks silently when resolving output/source paths; define and document behavior.
5. Tests using tiny generated images: zero length, truncated header, invalid partition bounds, overlapping partitions, sparse image, read at EOF, beyond EOF, and integer overflow.
6. Document the source interface for future E01/VHD/VMDK and device implementations, but do not add these formats unless explicitly assigned.
7. After the raw-image vertical slice works, prepare an E01 spike as a separate proposal: compare TSK's built-in EWF support and libewf on all host platforms, including split-segment images and integrity metadata. Do not merge an E01 backend until G confirms dependency/license/build feasibility and the lead accepts its fixture matrix.

**Handoff to:** C/D consume the source interface. A reviews any changes to its contract. G needs build/dependency metadata.

**Acceptance:** reading a source cannot mutate it; all reads are bounded; image hash is computed by F or shared utility with streaming and progress; invalid layouts fail safely with actionable diagnostics; same image yields identical bytes on all host OSes.

### Agent C — TSK adapter / NTFS, APFS, ext4

**Owns:** `adapters/tsk/`, TSK-specific build and license metadata.

**Implement in order:**

1. Pin a TSK version and build/import it consistently on Linux, macOS, and Windows. Record upstream version, build options, and license notices. TSK has multiple licenses in its tree; review the exact linked library files and dependencies before redistribution.
2. Use TSK 4.15.0 as the initial research baseline, then pin the exact version that passes our builds/fixtures. Confirm APFS, NTFS, and ext4 behavior from code/tests and controlled images; do not infer recovery support from the format name alone.
3. Create an adapter-local translation layer from TSK records to shared models. TSK types must not escape this module.
4. Implement volume/file-system detection and inventory for NTFS, APFS, ext4; return `unsupported`, `ambiguous`, and `encrypted/locked` statuses distinctly where detectable.
5. Enumerate allocated and deleted metadata entries and preserve metadata flags/timestamps/attributes that TSK exposes. Do not treat a deleted entry as recoverable until content extraction succeeds.
6. Read content via extents with safe bounds. Emit partial results if an extent is missing or unreadable.
7. Build test fixtures for NTFS, APFS, ext4 with known files, known deleted-file cases, Unicode names, empty/fragmented files, and at least one damaged/truncated case per feasible format.
8. Compare APFS behavior with libfsapfs on the same fixtures and write a short recommendation. Do not add libfsapfs to the shipped dependency set unless the lead accepts the feature gap, license, and cross-platform build evidence.
9. Write adapter limitations per file system and version; no generic “verified” boolean.

**Acceptance:** adapter passes through common API on all three hosts; known-file content matches fixture ground truth; deleted metadata and recovered bytes are reported separately; warnings explain unallocated/overwritten/encrypted limitations; no writes to the source.

### Agent D — XFS and Btrfs feasibility / experimental recovery

**Owns:** `adapters/xfs/`, `adapters/btrfs/`, `docs/filesystems/`, specialized fixtures.

**Implement in order:**

1. Audit TSK version behavior and current source/docs for XFS and Btrfs. Make a capability table: detect, enumerate allocated files, enumerate deleted metadata, read extents, checksums, snapshots/subvolumes, compression, multi-device support.
2. Produce a short feasibility note with testable facts, known gaps, alternatives and licenses. Avoid assuming the slide-deck design (walking free blocks) reflects actual parser capability.
3. Implement adapter wrappers using TSK where it passes fixtures. If TSK is insufficient, isolate any specialized parser in a separate adapter and propose the dependency/license to the lead before adding it.
   - For XFS, evaluate libfsxfs as an independent read-only cross-check; it is experimental, so it cannot be treated as a proven recovery engine without our own coverage.
   - For Btrfs, document whether a credible portable read-only parser exists. Linux kernel mounts/btrfs-progs may assist fixture generation and comparison but are not the cross-platform evidence backend.
4. For XFS, include varied allocation groups/metadata geometry and deleted/fragmented fixtures if the chosen parser claims to handle them.
5. For Btrfs, include checksummed/compressed files, snapshots/subvolumes, and multi-device images only if the implementation claims those capabilities.
6. Record unsupported features as capability flags and clear warnings, not silent omission.

**Acceptance:** no claim exceeds tested capability; tests record exact parser version and image generation procedure; experimental label is displayed in CLI/UI/report; malformed metadata cannot trigger out-of-bounds reads or writes.

### Agent E — Desktop UI and CLI

**Owns:** `apps/gui/`, `apps/cli/`, UI assets and user-facing strings.

**Implement in order:**

1. CLI commands: `version`, `inspect-image`, `scan`, `export-report`. Accept an image path and case/output directory; use stable exit codes and JSON-lines events for automation.
2. Qt 6 Widgets main window: recent/open case, choose image, choose output directory, show source hash/size after inspection, and require explicit scan start. Do not introduce QML, Electron, or Python into the production UI without an approved architecture decision.
3. Scan view: phase, determinate/indeterminate progress, pause only if truly supported (otherwise omit), cancel, logs/warnings, recovered candidates.
4. Results view: recovery status, path/name, size, parser source, confidence/limitations, computed output hash, and preview for safe supported formats. Preview must be read-only and must not execute recovered files.
5. Case/report screens: export manifest and human-readable report; show incomplete/cancelled scan state prominently.
6. Add localization-ready resource handling and keyboard/accessibility basics; avoid embedding parser logic or hashing in UI.

**Acceptance:** UI and CLI call the same `ScanService`; no direct TSK calls; CLI output schema documented and stable; selecting source and output to the same image is rejected; cancellation and errors are legible; preview cannot alter source or recovered artifact.

### Agent F — Case database, hashes, output, and report

**Owns:** `case/`, `reporting/`, recovery-output service, manifest schema.

**Implement in order:**

1. Propose a versioned JSON manifest and SQLite schema; review fields with core, parser, UI, and integrator before migration code.
2. Implement SQLite migrations, transaction boundaries, indexes, and deterministic case/job IDs. Store paths safely and define behavior if a case is moved.
3. Implement streaming SHA-256 for source image and recovered content; record algorithm and completion status. Do not label it source-block verification unless a parser-provided expected checksum was actually compared.
4. Safe output writer with collision handling, path traversal defense, reserved-name handling across OSes, partial temp files, atomic finalize where possible, and an explicit status for incomplete extraction.
5. JSON manifest export first, HTML second. PDF is follow-on; do not add ReportLab/WeasyPrint or another Python runtime. Propose a Qt-compatible renderer and licensing/build proof before implementing PDF.
6. Add tests for database reopen/migration, crash/interruption, duplicate filenames, Unicode names, path traversal names, partial data, hash test vectors, and export round-trip.

**Acceptance:** a case can be reopened on all supported platforms; export schema is versioned and deterministic where practical; hashes match standard SHA-256 vectors; output cannot escape chosen directory; incomplete content can never appear as complete.

### Agent G — Build, dependency management, CI, and release packaging

**Owns:** root CMake integration after initial scaffold, `.github/workflows/` (or chosen CI), `packaging/`, dependency/license inventory.

**Implement in order:**

1. Define supported compiler/toolchain matrix and CMake presets for MSVC, Apple Clang, Linux Clang/GCC; use C++17 as the baseline.
2. Build core and fake adapter on three OS runners early; add formatting/static checks and unit-test stages.
3. Integrate pinned TSK builds and cache/download strategy without unpinned release dependencies. Record licenses and notices.
4. Add platform integration jobs for image-based parser fixtures when dependencies permit; publish test artifacts/logs for failures.
5. Prototype Windows installer, macOS `.app`/`.dmg`, and Linux AppImage or `.deb`. Clearly separate unsigned CI artifacts from signed release artifacts.
6. Document code signing, macOS notarization, reproducible build metadata, versioning, and release checklist. Never store signing secrets in source or artifacts.

**Acceptance:** clean CI builds on Windows/macOS/Linux; dependency versions are pinned; user-facing artifacts install/run in smoke checks; license notices ship; release job cannot sign/publish from an untrusted branch.

## Parallelization map

| Phase | Can run in parallel | Required integration point |
| --- | --- | --- |
| Contract week | A drafts models; G prototypes compiler matrix; D audits XFS/Btrfs; F drafts schema; E mocks screens/CLI | Lead freezes shared IDs, errors, source/adapter/event contracts and manifest v1 |
| First vertical slice | B implements raw source; A implements job service/fake adapter; G builds CI; E builds CLI/UI shell; F implements manifest/hash utilities | Integrate a fake adapter scanning one generated raw image end-to-end |
| Parser bring-up | C integrates TSK; D validates XFS/Btrfs; B adds partition mapping; E connects job events; F persists results | All adapters return the same common result/event types |
| Recovery | C/D implement extraction; F implements safe writer/hash; E adds results screen | Fixture bytes/status/hash match ground truth |
| Release | G packages; all owners fix cross-platform failures; lead reviews support labels | Install/smoke matrix and known-limitations review complete |

Avoid parallel edits to shared schema/header files. A contract change is a blocking review item: proposer writes the change and migration note, affected owners approve it, then the lead merges it before dependent work continues.

## Integration gates (required before linking streams)

These gates are designed to catch incompatible assumptions before they become expensive to unwind. The lead records a gate as passed in the decision log with the commit, CI run, fixture IDs, and host OSes used. Do not merge a dependent feature past a failed gate.

### Gate 0 — technology and contract freeze

- Agent A publishes headers for `ReadOnlySource`, volume/partition models, parser adapter, findings, errors, scan request/events, and recovery state.
- Agent F publishes manifest v1 and the SQLite-to-manifest mapping.
- Agent C and D submit file-system capability proposals using A's model; they do not add parser-specific fields to common types by themselves.
- Agent E submits a CLI/UI event-consumer example; Agent G demonstrates a clean C++17 Qt/CMake build on the three host OSes.
- Agent 0 checks that no library is treated as a recovery guarantee, records open questions, and freezes the contracts. Any change after this point requires an explicit version/change note.

### Gate 1 — vertical slice with a fake adapter

- A fake adapter reads one small generated raw image through B's source and emits results through A's job API.
- E's CLI can run the fake scan and serialize the same typed events; Qt UI can display progress and terminal state.
- F persists the result and exports a manifest; the manifest validates against its schema.
- G builds and runs the same smoke flow on Windows, macOS, and Linux.

### Gate 2 — source and partition boundary

- B's raw source passes bounds, overflow, truncation, and read-only checks.
- Partition offsets are valid against image size; malformed layouts yield explicit warnings/errors.
- C/D demonstrate that parser adapters receive the same byte view and partition offsets on every host. No adapter opens a source path directly behind the source abstraction.

### Gate 3 — adapter capability acceptance

- Each file system has fixture IDs, expected inventory, expected recovered bytes, parser version, and known limitations.
- C validates NTFS/APFS/ext4; D validates XFS/Btrfs. Findings are translated into common models with no parser-native types crossing boundaries.
- Independent comparison is documented where available; where no independent parser exists, label the evidence limit rather than treating self-generated expected output as validation.
- The support/capability matrix is approved by Agent 0 before a format is labeled core or experimental in the UI.

### Gate 4 — end-to-end recovery and case integrity

- GUI and CLI use the same job service and recovery pipeline.
- Extracted bytes, partial/failure status, output path, SHA-256, warnings, and manifest agree for the same fixture.
- Cancellation/interruption leaves no output marked complete and does not corrupt the case database.
- Output path traversal and source/output collision checks pass before release packaging starts.

### Gate 5 — cross-platform release candidate

- A clean checkout builds and runs required automated tests on Windows, macOS, and Linux using pinned dependencies.
- Same fixture corpus produces the same file bytes and manifest semantics on each host; host-specific path formatting is normalized/documented.
- Each installer runs on a clean supported OS environment without a developer Qt install.
- License notices, dependency inventory, signing/notarization state, and known limitations accompany artifacts.

### Merge rules

1. Root `CMakeLists.txt`, CI workflow, dependency lock/pins, and packaging files have one editor/owner: G. Other streams submit requirements or adapter-local build fragments, not competing root changes.
2. Shared C++ models/events have one editor/owner: A. The JSON schema and DB migrations have one editor/owner: F. A pull request touching either requires review from the dependent owners plus Agent 0.
3. Merge API/schema changes before implementations that consume them. Do not merge all branches at once at the end.
4. Each PR must build against `main`, pass its scoped tests, and include at least one consumer/integration test for any changed contract. Cross-platform builds are mandatory for contract, source, adapter, and packaging changes before they are called complete.
5. Keep a small end-to-end fake-adapter smoke test in CI permanently. It detects broken linking even when parser fixtures are unavailable on a runner.
6. Do not accept a “supported filesystem” change without fixture evidence and a support-matrix update. Do not label an item verified solely because SHA-256 succeeded.

## Agent kickoff packet

Give each agent this plan plus the following task-specific instructions:

1. State the assigned workstream letter and exact owned directories.
2. Read the architecture and integration contracts before coding; ask the lead about a conflict, do not invent a private competing API.
3. Work only in owned directories plus tests/docs for that stream. Do not edit another agent's files without coordination.
4. Return a PR with: summary, files/components, build and test commands actually run, tested host OS, fixture provenance, limitations, dependencies/licenses, and follow-up issues.
5. Do not claim tests were run if they were not. Do not use personal or production storage as test data.
6. If blocked by a missing contract, submit a minimal proposal/example and continue independent tasks; do not wait by duplicating another stream.

## Imported diagram references and interpretation

The following files were supplied with the project and are preserved under `references/diagrams/`:

| File | Shows | How this plan uses it |
| --- | --- | --- |
| `DFDL0.jpeg` | High-level tool, investigator, target drive, reviewer context | Retained as initial stakeholder/context diagram; source access becomes image-first for safety and portability |
| `DFDL1.jpeg` | UI/config, parsing, verification, storage/reporting data flow | Maps to app/jobs, parser adapters, verification/output, and case/report modules |
| `DFDL2.jpeg` | XFS/Btrfs parser branch and extraction flow | Retained as filesystem-specific detail; parser support must be capability-tested, with experimental status |
| `class.jpeg` | UI manager, recovery engine, XFS/Btrfs parsers, verification, file record | Conceptual only; updated boundary is a shared adapter API, and hash computation is distinct from completeness/source-checksum validation |
| `usecase.jpeg` | Admin/investigator actions and legal reviewer | Maps to case user flows, scan, preview, report; legal reviewer consumes exported evidence, not a live app role |
| `activity.jpeg` | Launch/select drive, branch on XFS/Btrfs, report flow | Replace single-target-drive/XFS-Btrfs-only path with image inspect, auto-detection, supported adapter selection, safe extraction, and explicit warning path |
| `sequence.jpeg` | UI, parser, verification, DB, reporting sequence | Maps to job events and case persistence. Progress and candidate previews are UI events; persistence should not wait until all work finishes |

The diagrams are useful requirements inputs, not authoritative implementation contracts. The revised architecture extends them to NTFS/APFS/ext4, all three host OSes, read-only image analysis, cancellation/partial results, clear provenance, and feature-specific limits. In particular, “compare against source block checksums” is only valid where the file system stores an applicable checksum and the adapter can correctly map and verify those exact bytes. A SHA-256 of the recovered output alone is not proof that it is intact relative to the original.

## Integration order

1. Create repository skeleton, CMake build, shared core models, and a fake parser adapter.
2. Add raw-image source and a CLI that can open/hash an image and emit a manifest.
3. Integrate TSK through the adapter contract; enumerate files on controlled images.
4. Add extraction, output hashing, and partial/failure states.
5. Add Qt UI using the same job service.
6. Add XFS/Btrfs experimental support and its separate fixture coverage.
7. Add case database/report export, then platform packaging and install verification.
8. Consider direct device acquisition only after image workflows and read-only safety design are reviewed.

Keep `main` buildable. Integrate each stream against a fake adapter and generated fixture before combining all real parsers.

## Testing and acceptance criteria

Tests must use generated or legally redistributable images with known contents. Never use a live personal drive as a test fixture. Include deleted and non-deleted files, fragmented files where feasible, Unicode paths, empty files, large files, sparse/compressed/encrypted examples where supported, damaged metadata, and truncated images.

### First usable milestone

- App and CLI build on Windows, macOS, and Linux CI.
- Each can open a raw image and identify partitions/file systems without modifying the input.
- A scan can be cancelled; cancellation is represented accurately in output.
- Recovered output is saved outside the source and hashed with SHA-256.
- A versioned JSON manifest records source hash, tool/parser versions, findings, warnings, and recovery status.
- Tests compare recovered bytes and metadata against fixture ground truth.
- Unsupported/encrypted/ambiguous cases produce explicit limitations rather than false success.

### File-system support gate

Do not label a file system “supported” based on successful mounting or one happy-path test. For each declared supported version/feature set, record fixture provenance, expected results, actual results, known gaps, and parser version. XFS/Btrfs remain experimental until coverage and independent review are satisfactory.

### Safety invariants

- No file-system mount is required for image analysis.
- Source image/device API is read-only by construction.
- All output writes are confined to the chosen case/output directory.
- Every recovered item has a status (`complete`, `partial`, `failed`, or `cancelled`) and provenance.
- A computed SHA-256 is labeled as an output hash; it is not proof of completeness.
- Logs and reports never include secrets, passwords, or encryption keys.

## Deployment plan

- **Windows:** signed installer; verify runtime dependencies and elevated access only if/when device acquisition is implemented.
- **macOS:** universal or separate Apple Silicon/Intel builds; Developer ID signing and notarization for distribution.
- **Linux:** AppImage and one package format such as `.deb`; document required system libraries if not bundled.
- Bundle pinned parser dependencies and publish their license notices. Record build commit and dependency versions in app diagnostics and each case manifest.
- CI validates builds and image-based tests on all three OSes. File-system fixture coverage runs on every host OS where practical; host OS must not gate image parser selection.

## Risks and decisions to revisit

- **Parser completeness:** TSK coverage does not guarantee accurate undelete for every format/version. Maintain a tested support matrix.
- **XFS/Btrfs maturity:** initial support is experimental; do not promise complete recovery.
- **APFS encryption and hardware:** encrypted volumes may be inaccessible without credentials/keys; physical acquisition may require Apple-specific handling.
- **SSD/TRIM and copy-on-write behavior:** deleted content may no longer exist even when metadata remains.
- **Build/licensing:** verify redistribution terms for every dependency before bundling installers.
- **Forensic validation:** a hash proves byte identity after acquisition, not that acquisition captured a complete or uncontaminated source.
- **Scope control:** defer ReFS, direct-device acquisition, cloud storage, and advanced carving until the image-based core is stable.

## Technology review and revised decisions

Research review date: **2026-10-24**. These decisions replace the broad/original proposal stack where it conflicted with the chosen native C++ architecture. Recheck upstream versions, target OS minimums, and license terms before release.

| Technology / choice | Verdict | Decision and reason |
| --- | --- | --- |
| C++20 | Change to C++17 baseline | C++ is a good fit for calling TSK's native C API and building one native core. C++20 has no established requirement in this product plan. Standardize on C++17 initially; revisit only for a clear need. |
| Qt 6 | Keep; specify Qt Widgets | Qt's CMake deployment workflow covers desktop builds and provides platform deployment tooling. Widgets fit tables, forms, progress, and preview flows. Qt is dual licensed; plan distribution and compliance before shipping. |
| Electron.js | Do not use for v1 | It adds a web runtime and another native bridge around C/C++ parsers. No current product requirement justifies that extra packaging and security surface. Reconsider only if a web UI requirement appears. |
| PyQt6 / Python core | Do not use for the production core | Python remains useful for fixture generation and analysis scripts, but running the core in C++ avoids binding/ABI friction around TSK and keeps the parser, CLI, and GUI on one native API. |
| The Sleuth Kit (TSK) | Keep as the first parser backend, not the sole truth | TSK is a suitable cross-platform library baseline for common formats. Its published scope includes NTFS, APFS, and ext4; XFS/Btrfs support was introduced as experimental. Validate each specific behavior against known images and, for important results, an independent implementation. |
| libfsapfs | Evaluate as a comparison/optional adapter, not an automatic replacement | Upstream describes it as experimental with feature gaps (including snapshot and some encryption/storage configurations). Compare TSK and libfsapfs on explicit APFS fixtures before choosing a fallback. Do not claim broad APFS recovery from either library's format parser alone. |
| libfsxfs | Evaluate for XFS cross-checks, not as a proven undelete engine | Upstream describes it as experimental and lists supported XFS versions 4/5 with older versions unsupported. It may be useful as an independent read-only parser or oracle for selected metadata, but must pass our own extraction fixtures. |
| Btrfs parser | No independently validated alternative selected | Keep Btrfs behind a capability-limited adapter. Audit TSK and candidate libraries/tools; the Linux kernel and btrfs-progs can help create/check fixtures on Linux, but mounting through the host kernel is not a portable parser backend and must not be used as the evidence parser for cross-platform images. |
| SQLite | Keep for local case/job metadata | Good fit for a single-user local case catalog and transactions. It is not itself tamper evidence. Export a versioned manifest and content hashes; distinguish those checks from verified source acquisition or completeness. |
| `hashlib` from the proposal | Replace in native application | It is Python-specific and mismatches the C++ core. Use Qt `QCryptographicHash` for SHA-256, or a separately approved crypto dependency if the core later needs algorithms Qt does not provide. Stream large files and report whether hashing completed. |
| ReportLab / WeasyPrint | Defer; remove Python runtime assumption | They are Python-oriented and would add a second application stack. Deliver JSON manifest and HTML first. Add a PDF renderer later using a Qt-compatible path or a separately isolated renderer after dependency, licensing, and cross-platform tests. |
| CMake | Keep | Appropriate common build configuration for a C++/Qt core on MSVC, Apple Clang, and GCC/Clang. Keep dependency versions pinned and test clean builds on native runners. |
| Raw disk images | Keep as first input | Simplest portable, seekable image source for a safe MVP. Add E01 as a high-priority follow-on because it is a common forensic image format, but gate it on cross-platform tests and dependency/license review. TSK can consume EWF when built with the needed support; libewf is another option, but upstream currently marks it experimental, so compare before adopting. |
| Direct physical-drive scanning | Defer | OS access, mounted-volume behavior, permissions, and accidental writes complicate safety. Acquire/read an image first; implement direct acquisition only as a distinct OS-specific feature after source protections are reviewed. |

### Who validates each technology decision

| Agent | Technology decision they must validate | Required output before adoption |
| --- | --- | --- |
| 0 | Overall architecture, product license/distribution assumptions, and whether proposed replacements solve an actual gap | Decision log entry; no dependency is adopted solely because one agent prefers it |
| A | C++17 common API and boundaries between core, Qt, and parser libraries | Compiling fake adapter/CLI example; API stays free of Qt Widgets and TSK types |
| B | Raw image source first; E01 as a gated follow-on using TSK EWF support versus libewf | Cross-platform read tests, segment/integrity behavior, dependency/build/license proposal before E01 merge |
| C | TSK 4.15.0 baseline for NTFS/APFS/ext4; compare APFS fixtures with libfsapfs | Feature matrix per format; fixture results and recommendation, including unsupported/locked cases |
| D | Experimental TSK XFS/Btrfs and libfsxfs as XFS comparison | Feasibility memo, limitations, independent comparisons and fixture proof; no broad recovery promise |
| E | Qt 6 Widgets/CMake; reject Electron/QML/Python UI unless requirements change | UI and CLI consume shared job API; clean host builds; no parser or hash logic in presentation layer |
| F | SQLite, manifest, Qt SHA-256, JSON/HTML first, PDF later | Migration/schema contract, standard hash vectors, safe-output tests, renderer dependency proposal if PDF is needed |
| G | CMake/toolchains, Qt packaging, dependency pinning, and license/SBOM inventory | Three-host build/package evidence; exact notices and pinned dependencies; identify unresolved release obligations |

An agent may recommend a change, but only Agent 0 records it as accepted after affected owners review it. If a technology fails its gate, implementers report the failure and stop that dependency path; they do not silently substitute a library or alter the common API.

### Parser backend policy

Use a capability matrix per adapter and per format feature. The app should ask an adapter about the exact operation it supports (detection, metadata listing, deleted-record discovery, extent extraction, compression, decryption, checksum verification); a single `supportsFilesystem` boolean is too coarse. Treat parser results as claims to validate, not ground truth. Maintain independent fixture expectations, and where possible compare metadata/content against a second implementation. Do not use the same parser to generate a fixture's expected output and then call that an independent validation.

TSK's release history lists 4.15.0, while the feature announcement for 4.13 described XFS/Btrfs as experimental. The first implementation task must inspect the pinned release's source/tests and produce actual capability results. The generic upstream support list alone is insufficient to certify APFS or undelete capabilities. TSK itself advises recreating important results with a second tool; reflect that principle in the validation workflow.

### Revised dependency and test policy

- Begin with raw images and the minimum dependencies: Qt Core/Widgets, TSK, SQLite, and CMake toolchain. Add packages only for a documented input or feature.
- Keep SHA-256 computation in one shared service, stream data, and use standard test vectors. Record source image hash separately from recovered file hashes.
- Keep JSON and HTML reports independent of Python. PDF is a follow-on deliverable and must not delay correct machine-readable results.
- Add compiler sanitizers and parser fuzz targets where the selected compiler/tooling supports them. Fuzz untrusted parser inputs and enforce memory/size limits; do not substitute fuzzing for fixture-based semantic validation.
- Generate ext4/XFS/Btrfs test images with documented tools and commands in a controlled Linux environment. Generate APFS fixtures on macOS using documented system tools. Store fixture hashes, creation procedure, expected metadata/content, and licensing/provenance next to each fixture. Run the same read-only analysis corpus on Windows, macOS, and Linux builds.
- Maintain a software bill of materials and exact per-file license inventory. Qt's OSS edition is under LGPL/GPL terms depending on module; TSK and optional libyal libraries also contain licensing conditions. Do not assume that a dependency is redistributable because its repository is public.

### Research references for technology decisions

- TSK upstream scope and supported image/file-system formats: <https://sleuthkit.org/sleuthkit/desc.php>
- TSK 4.15.0 release: <https://github.com/sleuthkit/sleuthkit/releases/tag/sleuthkit-4.15.0>
- TSK experimental XFS/Btrfs announcement: <https://github.com/sleuthkit/sleuthkit/discussions/3184>
- TSK recommends corroborating important findings with a second tool; see its upstream README: <https://github.com/sleuthkit/sleuthkit>
- Qt CMake desktop deployment API: <https://doc.qt.io/qt-6/cmake-deployment.html>
- Qt 6 C++17 minimum requirement: <https://doc.qt.io/qt-6/cmake-get-started.html>
- Qt SHA-256 API (`QCryptographicHash`): <https://doc.qt.io/qt-6/qcryptographichash.html>
- Qt licensing options and LGPL/GPL module notes: <https://doc.qt.io/qt-6/licensing.html>
- libfsapfs project status/feature limits: <https://github.com/libyal/libfsapfs/blob/main/README>
- libfsxfs status, versions, and unsupported formats: <https://github.com/libyal/libfsxfs/blob/main/README>
- libewf status and EWF formats: <https://github.com/libyal/libewf>
- Apple APFS feature overview: <https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/Features/Features.html>
- Microsoft ReFS overview: <https://learn.microsoft.com/en-us/windows-server/storage/refs/refs-overview>

## External technical references

- The Sleuth Kit overview and listed file-system/image support: <https://sleuthkit.org/sleuthkit/desc.php>
- The Sleuth Kit downloads/releases: <https://www.sleuthkit.org/sleuthkit/download.php>
- Sleuth Kit release information for experimental XFS/Btrfs work: <https://github.com/sleuthkit/sleuthkit/discussions/3184>
- Apple APFS features, encryption, and copy-on-write behavior: <https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/Features/Features.html>
- Microsoft ReFS overview and comparison with NTFS: <https://learn.microsoft.com/en-us/windows-server/storage/refs/refs-overview>

Architecture and source support links checked 2026-10-24. Re-check parser versions, feature gaps, and dependency licenses when implementation starts.
