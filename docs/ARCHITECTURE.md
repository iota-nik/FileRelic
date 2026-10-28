# FileRelic Architecture: Current Stage

FileRelic will have two user interfaces that share one recovery engine:

```text
Command-line interface ─┐
                        ├─ inspection / recovery engine ── raw image reader
Desktop GUI ────────────┘
```

The command-line interface is for repeatable commands, automation, and testing. A future Qt desktop GUI will offer buttons, progress views, and file previews. The GUI must call the same inspection and recovery functions as the CLI; it must not run a separate implementation or parse terminal output.

## Current implementation

- `src/cli_main.cpp` is the CLI front door. It interprets commands such as `inspect-image`.
- `src/image_inspector.h` declares the callable image-inspection API.
- `src/image_inspector.cpp` implements that API and is built as `filerelic_core`.
- `filerelic::inspect_image(path)` opens a file read-only and returns an
  `InspectionReport`. The report owns the source image path, byte size, success
  status, typed partition-table and partition records, filesystem metadata,
  warnings, presentation sections, and the current human-readable diagnostic
  trace.
- `filerelic::render_report(report, output)` is the CLI presentation layer. It
  writes an existing report to a terminal but does not perform parsing itself.
- `gui/` contains a Qt desktop shell linked to the same `filerelic_core`
  library as the CLI. It calls `inspect_image` on a worker thread and then
  displays the returned report. It no longer starts a separate CLI process.

The parser now fills typed partition and filesystem records while reading the
image. The GUI builds cards from those records through `InspectionReport`;
the CLI keeps its existing text format through the same report object's raw
trace. The raw trace is written into a per-inspection stream, not by
redirecting process-wide `std::cout`/`std::cerr`. Future work can replace that
compatibility trace with renderers over the typed records.

The code remains in a small number of source files while we learn the concepts.
The inspection implementation now builds as a core library target. The public
API prevents later parser reorganizations from changing how the CLI or GUI asks
for an inspection.

## Running the CLI

`./build/filerelic` is a normal compiled CLI program. It feels like a local script right now because it has not been installed into a system command directory yet. During development, running it from `build/` is normal:

```bash
./build/filerelic inspect-image testdata/practice-mbr.raw
```

Later, CMake packaging will install `filerelic` into a location on your shell `PATH`, allowing this form from any directory:

```bash
filerelic inspect-image /path/to/image.raw
```
