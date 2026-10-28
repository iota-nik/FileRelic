# FileRelic GUI

The Qt desktop application and CLI now link the same `filerelic_core` library.
The GUI calls `filerelic::inspect_image()` directly on a worker thread; it does
not start the CLI. It builds cards from typed partition, filesystem, warning,
and summary records in `InspectionReport`; the CLI renders its compatible text
report from that same inspection call. The optional Advanced view shows the
raw diagnostic trace.

## What it does

- Lets the user choose a disk image (`.raw`, `.img`, `.dd`, `.dmg`, or any file).
- Inspects the image through the shared read-only core API.
- Displays image, partition, filesystem, and warning details in readable cards.
- Keeps the detailed diagnostic text available in the Advanced section.
- Runs the inspection off the UI thread so the window can remain responsive.

This is still an inspection front-end, not a file recovery tool. It never writes
to the selected image.

## Build requirements

- CMake 3.16 or later
- C++17 compiler
- Qt 6 Widgets and Concurrent modules

From the repository root:

```bash
cmake -S . -B build
cmake --build build
cmake -S gui -B build-gui
cmake --build build-gui
./build-gui/filerelic-gui
```

Qt is not bundled with the repository. The GUI build uses standard CMake
automoc support and is compatible with Qt 6.2 and later.

## Current technical limit

The current records include partition table kind/state, partition type and
bounds, detected filesystem kind, and selected ext metadata. They do not yet
represent every GPT/MBR validation detail or every ext directory/inode result.
The raw text trace remains for CLI compatibility; a later step can render that
trace from the typed report and remove the legacy output path.
