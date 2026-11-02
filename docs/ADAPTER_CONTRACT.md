# Filesystem adapter contract (DRAFT for group review)

Status: **proposal, not implemented.** Nothing in the code uses this yet. Freeze
it before writing the Sleuth Kit (TSK) adapter. Goal: no third-party type ever
appears in `image_inspector.h`, the CLI, or the GUI.

## What exists today

`ReadOnlySource` (`src/image_source.h`) is the one way to read image bytes. The
ext reader already takes a source plus a partition start offset, which is the
shape every adapter needs. It also already implements `list_allocated`,
`read_file_content` and `list_deleted_metadata` for ext2/3/4 (`ExtVolume::list_deleted`,
statuses in `DeletedStatus`) and a deleted-content read (`recover_deleted_content`: bitmap-checked,
zero-fills reused blocks, PARTIAL status, journal fallback). That is a working reference for what an
adapter must report for deleted entries: name evidence plus an honest status, and a recovery result
that separates "bytes read" from "proven original".

## Proposed interface

```cpp
struct VolumeRange { std::uint64_t start_byte; std::uint64_t length_bytes; };

enum class Capability {
    detect, list_allocated, list_deleted_metadata, read_file_content, checksum_verify
};

struct AdapterInfo {
    std::string id;                    // "builtin-ext-fixture", "tsk"
    std::string version;
    std::vector<Capability> supported; // per adapter AND per filesystem, never one bool
};

class FileSystemAdapter {
public:
    virtual ~FileSystemAdapter() = default;
    virtual AdapterInfo info() const = 0;
    // Fills FileSystemInfo (incl. limitations) for one volume. Read-only.
    virtual bool inspect(ReadOnlySource& source, VolumeRange volume,
                         FileSystemInfo& out) = 0;
    // Later: enumerate(volume, callback) -> FileEntry records; read(entry) -> bytes.
};
```

## Rules

1. Adapters read only through `ReadOnlySource` (TSK needs a custom `TSK_IMG_INFO`
   backed by it, so it never opens the path itself).
2. TSK headers and types stay inside `adapters/tsk/`.
3. Each adapter maps into FileRelic structs; unknown stays unknown.
4. Capabilities are reported honestly: listing deleted metadata is not
   recovery; recovery is claimed only after fixture evidence.
5. Warnings and limitations go into the report, never silently dropped.

## Open questions for the group

- Does the ext reader become a "builtin" adapter, or stay a teaching fixture reader?
- Callback vs. vector for large directory listings?
- How is a `FileEntry` identified across adapters (inode / MFT ref / path)?
