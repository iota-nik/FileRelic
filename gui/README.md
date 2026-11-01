# FileRelic GUI

The Qt desktop application and the CLI link the same `filerelic_core` library.
The GUI calls `filerelic::inspect_image()` directly on a worker thread; it does
not start the CLI. Result cards are built from the typed records in
`InspectionReport` (partitions, filesystems, ext root-directory entries,
warnings). The optional Advanced view shows the scanner's step trace, which is
a log only: no card reads it.

## What it does

- Lets the user choose a disk image (`.raw`, `.img`, `.dd`, `.dmg`, or any file).
- Inspects the image through the shared read-only core API.
- Shows image, partition, filesystem, root-directory, and warning details as cards.
- Runs the inspection off the UI thread so the window stays responsive.

This is still an inspection front-end, not a file recovery tool. It never writes
to the selected image.

## Build

The GUI is part of the single root build. If Qt 6 (Widgets + Concurrent) is
installed, it is built automatically; otherwise only the CLI is built.

```bash
cmake -S . -B build
cmake --build build
./build/filerelic --help
./build/gui/filerelic-gui
```

Ubuntu 22.04/24.04 needs: `sudo apt install qt6-base-dev`.
Skip the GUI explicitly with `cmake -S . -B build -DFILERELIC_BUILD_GUI=OFF`.
