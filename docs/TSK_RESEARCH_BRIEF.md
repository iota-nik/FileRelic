# Research brief: The Sleuth Kit for FileRelic (for one group member)

Do not change shared code (`src/`, `CMakeLists.txt`). Return a 1–2 page
recommendation, with sources and the date checked.

Questions to answer:

1. **Version and build.** Current TSK release? How is it built on Linux,
   Windows (MSVC), and macOS with CMake? Is there a usable package (vcpkg,
   Homebrew, apt) or must we vendor/build it? C++17 compatibility of its headers.
2. **License.** Exact licenses of the files we would link (TSK mixes licenses).
   What do we owe if we ship binaries with a Qt app?
3. **Capability per filesystem (ext4, NTFS first).** Using TSK's own docs and a
   test image, record: detect, list allocated files, list deleted entries,
   read content (including ext4 extents), timestamps. Label each *verified by
   test* vs *claimed in docs*.
4. **Custom image source.** Can TSK read from a custom backend (to wrap our
   `ReadOnlySource`), or only from file paths? What is the API (`tsk_img_open`
   variants, custom `TSK_IMG_INFO`)?
5. **Fixtures.** Command lines to build small ext4 and NTFS images with known
   files and one deleted file, without mounting evidence or using personal data
   (e.g. `mke2fs -d`, `mkntfs`). Record exact commands and tool versions.
6. **Independent check.** Which second tool can cross-check TSK's results
   (e.g. `debugfs` for ext4, `ntfsinfo`/`ntfscat`)?

Deliverable format: a markdown file in `docs/` named `TSK_FINDINGS.md`.
