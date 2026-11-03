# FileRelic GUI

The Qt desktop application and the CLI link the same `filerelic_core` library.
The GUI calls `filerelic::inspect_image()` directly on a worker thread; it does
not start the CLI. Result cards are drawn from the `ReportSection`s that
the core builds in `InspectionReport::sections` (the same ones the CLI prints). The optional Advanced view shows the scanner's step trace, which is
a log only: no card reads it.

## What it does

- Lets the user choose a disk image (`.raw`, `.img`, `.dd`, `.dmg`, or any file).
- Inspects the image through the shared read-only core API.
- Draws the core's plain-language `ReportSection`s as cards (green = good, amber = warning, red = problem) with label/value rows.
- "Show technical details" re-renders the same report with sector numbers, IDs and features.
- `filerelic-gui <image>` opens and inspects an image immediately.
- Automatically saves a versioned JSON investigation manifest after inspection, including failed inspections and their diagnostics.
- Browses directories on detected ext2/3/4 partitions, can show deleted-name candidates, and extracts the selected file, directory, or deleted candidate to the chosen output folder.
- Records each extraction's status, output path, byte count, SHA-256, and notes in the same manifest.
- Shows indeterminate progress while inspection or extraction is running; the core does not yet support cooperative cancellation, so there is no cancel control.
- Still offers manual manifest export as a separate copy.
- Runs the inspection off the UI thread so the window stays responsive.

The GUI writes reports and recovered files only to the selected output directory, never
to the image. Deleted-name entries are candidates, not promises of successful recovery.

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
