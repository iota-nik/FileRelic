# FileRelic Architecture: Current Stage

FileRelic will have two user interfaces that share one recovery engine:

```text
Command-line interface ─┐
                        ├─ inspection / recovery engine ── raw image reader
Desktop GUI ────────────┘
```

The command-line interface is for repeatable commands, automation, and testing. A future Qt desktop GUI will offer buttons, progress views, and file previews. The GUI must call the same inspection and recovery functions as the CLI; it must not run a separate implementation or parse terminal output.

## Current implementation

- `src/main.cpp` is the CLI front door. It interprets commands such as `inspect-image`.
- `src/image_inspector.h` declares the callable image-inspection API.
- `filerelic::inspect_image(path)` opens a file read-only and returns an
  `InspectionReport`. The report owns the source image path, byte size, success
  status, and the current canonical human-readable inspection text.
- `filerelic::render_report(report, output)` is the CLI presentation layer. It
  writes an existing report to a terminal but does not perform parsing itself.
- `gui/` contains a Qt desktop shell. It currently starts the CLI as a safe
  temporary bridge; its next integration step is to render `InspectionReport`
  directly.

The initial report deliberately keeps the detailed report text because it lets
the existing parser move behind one shared boundary without changing its
validated output. The next refinement adds typed partition, filesystem, and
warning objects to `InspectionReport`; then the GUI can show cards and tables
without interpreting terminal strings.

The code remains in a small number of source files while we learn the concepts.
Later, the inspection implementation will move into a core library target. The
public API prevents that reorganization from changing how the CLI or GUI asks
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
