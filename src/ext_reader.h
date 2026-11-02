#pragma once

// ext2/3/4 reader.
//
// Understands: superblock, 32/64-bit group descriptors, inodes in any block
// group, ext4 extent trees (any depth up to a safety limit), ext2/3 block maps
// (direct + indirect), linear scan of directories, path lookup, and reading
// regular-file content (sparse holes and unwritten extents read as zeros).
//
// Deleted-entry listing: finds names left behind in directory blocks (a removed
// entry's bytes stay in the previous entry's slack space) and judges each one
// from its inode and the block bitmap. It never reads or recovers file content.
//
// Does NOT understand (reported as limitations, never faked): inline data,
// encryption, journal replay, symbolic-link targets, external journals,
// deleted inodes that no directory entry names any more.
//
// Recovery of a deleted regular file (recover_deleted_content): reads the blocks
// the deleted inode still lists, or, if the filesystem erased that map, an older
// copy of the inode found by scanning the journal area (read-only). Blocks that
// are in use again are zero-filled and the result is PARTIAL.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "scan_context.h"

namespace filerelic {

// Receives file bytes in order. Return false to abort (e.g. disk full).
using ByteSink = std::function<bool(const std::uint8_t*, std::size_t)>;

enum class ReadStatus { complete, partial, failed };

struct ContentReadResult {
    ReadStatus status = ReadStatus::failed;
    std::string error;                    // why it is not complete, if it is not
    std::vector<std::string> notes;       // non-fatal remarks (e.g. sparse file)
    std::uint64_t expected_size = 0;      // size recorded in the inode
    std::uint64_t bytes_delivered = 0;    // bytes actually handed to the sink
};

class ExtVolume {
public:
    using Trace = std::function<void(const std::string&)>;

    // Parses the superblock at partition_start. Fills `info` (label, UUID, block
    // size, ...). Returns nullptr and sets `error` when the volume is unusable.
    static std::unique_ptr<ExtVolume> open(ReadOnlySource& source,
                                           std::uint64_t partition_start,
                                           FileSystemInfo& info,
                                           std::string& error,
                                           Trace trace = {});

    struct Resolved {
        bool ok = false;
        std::string error;
        std::uint32_t inode_number = 0;
        std::string name;                 // last path component as raw bytes
        InodeInfo inode;
    };

    Resolved root();
    // Walks a '/'-separated path from the root. Symbolic links are not followed.
    Resolved resolve(const std::string& path);
    // Lists a directory (capped; sets listing.limitation if truncated or damaged).
    void list(const InodeInfo& directory, DirectoryListing& listing);
    // Finds unlinked entries whose names are still in this directory's blocks.
    // Each gets an honest DeletedStatus; nothing is claimed recoverable.
    void list_deleted(const InodeInfo& directory, DeletedListing& listing);
    // How a deleted file's content was obtained (filled by recover_deleted_content).
    struct RecoveryDetails {
        std::string source;                      // "the deleted inode" or "an older copy ... journal"
        bool from_journal = false;
        std::uint32_t journal_sequence = 0;
        std::uint64_t total_blocks = 0;          // data blocks the block map lists
        std::uint64_t reused_blocks = 0;         // of those, in use by something else now
        std::uint64_t unverifiable_blocks = 0;   // outside the volume / bitmap unreadable
    };
    // Streams the bytes of a deleted regular file. Uses the deleted inode's block
    // map, or an older copy from the journal when the filesystem erased it. Blocks
    // now owned by other data are zero-filled and the result is PARTIAL. Refuses
    // when the inode number belongs to a live file. Never claims the bytes are the
    // original file; "complete" only means every recorded block was read and free.
    ContentReadResult recover_deleted_content(std::uint32_t inode_number, const ByteSink& sink,
                                              RecoveryDetails& details);
    // Streams a regular file's bytes to `sink`.
    ContentReadResult read_content(const InodeInfo& file, const ByteSink& sink);

private:
    struct Geometry {
        std::uint32_t block_size = 0;
        std::uint16_t inode_size = 0;
        std::uint32_t inodes_per_group = 0;
        std::uint16_t descriptor_size = 32;
        bool is_64bit = false;
        std::uint32_t inodes_count = 0;
        std::uint64_t blocks_count = 0;
        std::uint32_t first_data_block = 0;
        std::uint32_t blocks_per_group = 0;
        bool has_journal = false;
        std::uint32_t journal_inode = 0;
    };
    struct RawInode {
        std::uint16_t mode = 0;
        std::uint16_t links_count = 0;
        std::uint32_t dtime = 0;
        std::uint32_t flags = 0;
    };
    struct JournalInodeCopy {
        std::uint32_t sequence = 0;
        std::uint64_t journal_block = 0;
        std::uint16_t mode = 0;
        std::uint16_t links_count = 0;
        std::uint32_t dtime = 0;
        InodeInfo inode;
    };
    using BitmapCache = std::map<std::uint64_t, std::vector<std::uint8_t>>;
    enum class BlockState { free_block, allocated, unknown };
    struct MapState;

    ExtVolume(ReadOnlySource& source, std::uint64_t start, Geometry geometry, Trace trace)
        : source_(source), start_(start), geometry_(geometry), trace_(std::move(trace)) {}

    void trace(const std::string& line) const { if (trace_) trace_(line); }
    InodeInfo read_inode(std::uint32_t inode_number);
    InodeInfo decode_inode(const std::uint8_t* inode);
    std::string find_journal_copies(std::uint32_t inode_number, std::vector<JournalInodeCopy>& out);
    bool read_raw_inode(std::uint32_t inode_number, RawInode& out);
    BlockState block_state(std::uint64_t block, BitmapCache& cache);
    void judge_deleted(DeletedEntryInfo& entry, BitmapCache& cache);
    bool locate_inode(std::uint32_t inode_number, std::uint64_t& offset, std::string& reason);
    void walk_extent_node(const std::uint8_t* node, std::size_t node_size, int levels_left,
                          std::vector<ExtentInfo>& out, std::string& limitation);
    void map_block_pointer(MapState& state, std::uint32_t pointer);
    void map_indirect(MapState& state, std::uint32_t pointer, int level);
    // Calls visit(inode, raw_name) for each entry; stops when visit returns false.
    // Returns a limitation message ("" when the whole directory was scanned).
    std::string for_each_entry(const InodeInfo& directory,
                               const std::function<bool(std::uint32_t, const std::string&)>& visit);

    ReadOnlySource& source_;
    std::uint64_t start_;
    Geometry geometry_;
    Trace trace_;
};

// Shows a raw on-disk name safely: valid UTF-8 is kept, anything else becomes '?'.
std::string printable_name(const std::string& raw);

// Used by the partition scanner: fills FileSystemInfo and the root directory
// listing, logging steps to the scan trace.
void read_ext_metadata(ScanContext& context,
                       std::uint64_t partition_start,
                       FileSystemInfo& file_system);

} // namespace filerelic
