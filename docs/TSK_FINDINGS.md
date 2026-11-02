# TSK findings for FileRelic

Checked: 2026-11-03
Sources: `https://sleuthkit.org/sleuthkit/`, `https://github.com/sleuthkit/sleuthkit`, `apt-cache show libtsk-dev`, and the upstream project documentation for `fls`, `ils`, `icat`, `blkls`, and `tsk_img_open`.

## 1) Version and build

- The upstream project is The Sleuth Kit (TSK), and the live project advertises itself as open-source forensic tooling for disk and filesystem analysis.
- The Ubuntu package metadata we checked exposes `libtsk-dev` version `4.11.1+dfsg-1` and the `sleuthkit` package. This is a practical version target for Linux builds.
- TSK is a conventional C/C++ project built from source and used via command-line tools; the project and website describe it as buildable on Linux and Windows/macOS with platform-specific toolchains. We should not assume a single one-line CMake recipe without vendoring or checking the exact upstream branch we would use.
- For FileRelic, the safest path is: vendor a pinned TSK release or build as a system dependency in CI, then validate the exact headers and APIs on the target OS. The project is not a plugin-based C++17 library with a tiny stable API surface; it is a forensic toolkit with substantial OS/toolchain variance.

## 2) License impact

- The TSK website explicitly states that the source is distributed under several licenses, and that the core code is under the IBM Public License / Common Public License arrangement, while other utilities and bundled components may have their own terms.
- The website also warns that a stripped-down GNU binutils strings component is included under GPL 2, but it is not the main library.
- Practical conclusion: linking TSK into a closed-source or commercial Qt app is not automatically forbidden, but legal review is still required. We should treat the TSK dependency as a licensed third-party component, preserve attribution, and confirm the exact build artifact and license notices before shipping binaries.

## 3) Capability per filesystem

The upstream docs are clear that TSK has broad support for Unix and NTFS-like volumes. The important distinction is between detection, listing, and deleted-data recovery.

| Capability | ext4 | NTFS | Status |
| --- | --- | --- | --- |
| Detect filesystem | Yes | Yes | Verified by docs and common TSK usage |
| List allocated files | Yes | Yes | Verified by docs and common TSK usage |
| List deleted names / metadata | Yes (`fls`, `ils`) | Yes (`fls`, `ils`) | Verified by docs |
| Read file content | Yes (`icat`, `blkcat`) | Yes (`icat`, `ntfsls`/`ntfscat` family) | Verified by docs |
| Recover deleted content from slack or unallocated blocks | Yes | Yes | Claims in docs, should be verified with fixtures |
| ext4 extent mapping | Yes | N/A | Verified by docs and file-system feature support |
| NTFS MFT metadata, deleted entry names | N/A | Yes | Verified by docs |

Important caveat: TSK is strong for file and metadata enumeration; FileRelic should still verify recovered content against a second tool. The TSK docs themselves say results should be recreated with a second tool to verify the data.

## 4) Custom image source / adapter boundary

- TSK is designed around image files and volume analysis, not a direct drop-in adapter for a custom `ReadOnlySource` object.
- The usual API model is the `tsk_img_open` family, which expects an image handle created by the library or a custom image backend. For a custom backend, the project uses a custom `TSK_IMG_INFO`-like wrapper.
- For FileRelic, this means the adapter boundary must remain explicit: TSK stays inside its own adapter layer, not leaking through the shared report model.
- The safest rule is: FileRelic exposes `ReadOnlySource` and its typed result structs, while the TSK adapter converts TSK metadata into FileRelic-native records and limitations.

## 5) Fixtures and exact commands

The project already includes a good pattern for ext4 fixtures using `mke2fs` and `debugfs`. For TSK evaluation, use the same approach:

```bash
# ext4 fixture (known files, deleted file, journal present)
mkfs.ext4 -F -b 1024 -L FileRelicExt4 /tmp/evidence.ext4
mkdir -p /tmp/ext4-root
cp known-file /tmp/ext4-root/
# optionally create a subdirectory and a deleted file
mount -o loop /tmp/evidence.ext4 /mnt/test
# copy files, delete a known file, sync, unmount
umount /mnt/test

# use TSK to enumerate the filesystem
after downloading or installing TSK:
fls -r -f ext /tmp/evidence.ext4
ils -f ext /tmp/evidence.ext4
icat -f ext /tmp/evidence.ext4 2
```

For NTFS, a comparable procedure is:

```bash
mkntfs -F -Q -L FileRelicNTFS /tmp/evidence.ntfs
mkdir -p /tmp/ntfs-root
# copy known files
# create a deleted file, then remove it
# use the TSK NTFS tools:
fls -r -f ntfs /tmp/evidence.ntfs
istat -f ntfs /tmp/evidence.ntfs <MFT-entry>
```

The key is to use a scratch image, not a real drive, and to keep the fixture names and file sizes known.

## 6) Independent validation tools

- For ext4: `debugfs` is the obvious second check. It can walk the filesystem structure and verify inode metadata, directory entries, and deleted entries.
- For NTFS: `ntfsinfo`, `ntfsls`, and `ntfscat` are the natural verification tools.
- For the FileRelic project, this means TSK should be treated as a candidate parser, not a final authority. We should still check the same file or inode with `debugfs`/`ntfs*` before trusting a recovery claim.

## Recommendation

TSK is a useful second-stage adapter target for FileRelic, especially for NTFS and for a more mature deleted-file workflow. The project should not treat TSK as a replacement for the current ext reader; it should treat TSK as a bounded adapter for inventory and cross-checking.

The right next step is to freeze the adapter contract (`docs/ADAPTER_CONTRACT.md`), then implement a TSK-backed adapter that maps TSK output into FileRelic-native `FileSystemInfo`, `DirectoryListing`, and deleted-entry results, with honest limitations and a second-tool verification requirement before claiming recovery.
