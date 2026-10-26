# FileRelic GUI shell

This is FileRelic's first desktop interface. It deliberately owns no forensic
parsing logic: it starts the existing `filerelic inspect-image <path>` command,
captures its report, and displays it. The CLI opens image files read-only, so
the GUI preserves the same safety boundary.

Keeping the bridge narrow is intentional for the presentation milestone. The
core scanner is still becoming a structured library API. Once it returns a
machine-readable `InspectionReport`, this process-based bridge will be replaced
by a direct call from the GUI to that shared core; the buttons and screen design
can remain.

## What it does now

- Lets a user choose a disk image (`.raw`, `.img`, `.dd`, `.dmg`, or any file).
- Runs `filerelic inspect-image` without write arguments.
- Streams the CLI's readable MBR, EBR, GPT, and filesystem-signature report.
- Clearly shows completion or failure and tells the user the image was not
  modified.

It is an inspection front-end, not a recovery or disk-writing tool.

## Build requirements

- CMake 3.16 or later
- C++17 compiler
- Qt 6 with the Widgets module
- A separately built FileRelic CLI executable

Qt is not bundled into this repository. On a machine with Qt 6 installed, build
the shell from this directory:

```bash
cmake -S gui -B build-gui -DFILERELIC_CLI_PATH="$PWD/build/filerelic"
cmake --build build-gui
./build-gui/filerelic-gui
```

The GUI build deliberately uses only standard CMake automoc support, so it is
compatible with Qt 6.2 supplied by Ubuntu 22.04 as well as later Qt 6 releases.

`FILERELIC_CLI_PATH` is the absolute path to the core CLI. If omitted, the GUI
expects `filerelic` beside its own executable.

## Why use the CLI temporarily?

The CLI already exposes a tested, read-only path through the parser. Calling it
from a `QProcess` is a small adapter that lets the presentation GUI progress in
parallel without duplicating partition parsing or racing changes in `src/`.
The next core milestone is a typed result model; after that, both the CLI and
this GUI should render the same report object directly.

## Current limits

- Requires Qt 6; this repository's primary CMake build does not yet add the GUI
  target.
- Shows the CLI's text report rather than a graphical partition map.
- It cannot recover files yet, and it intentionally cannot write to images.
