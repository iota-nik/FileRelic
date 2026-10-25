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
- `filerelic::inspect_image(path)` is the first shared engine function. It opens a file read-only and performs the current MBR/EBR inspection.

The code remains in a small number of source files while we learn the concepts. Once GPT support begins, the inspection implementation will move to its own C++ source file and then into a core library target. The public API prevents that reorganization from changing how the CLI or future GUI asks for an inspection.

## Running the CLI

`./build/filerelic` is a normal compiled CLI program. It feels like a local script right now because it has not been installed into a system command directory yet. During development, running it from `build/` is normal:

```bash
./build/filerelic inspect-image testdata/practice-mbr.raw
```

Later, CMake packaging will install `filerelic` into a location on your shell `PATH`, allowing this form from any directory:

```bash
filerelic inspect-image /path/to/image.raw
```
