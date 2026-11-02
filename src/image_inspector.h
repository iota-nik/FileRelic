#pragma once

// Public API of the FileRelic inspection core.
//
// inspect_image() returns an InspectionReport. The CLI and the Qt GUI both draw
// their output from that one object; neither parses the other's text.
// Values that could not be determined stay explicit (std::optional, empty
// strings, or a `limitation` message) instead of being faked as zero.

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace filerelic {

enum class ReportSectionKind {
    summary,
    image,
    partition_layout,
    partition,
    directory_listing,
    warnings,
    diagnostic,
};

enum class PartitionTableKind { unknown, mbr, gpt };
enum class FileSystemKind { unknown, ntfs, apfs, xfs, ext_family, btrfs };
enum class ValidationState { unknown, valid, warning, invalid };

enum class InodeKind { unknown, regular_file, directory, symbolic_link, other };
enum class DataMapping { unknown, direct_blocks, extent_tree, none };

// One contiguous run of blocks belonging to a file (ext4 extent).
struct ExtentInfo {
    std::uint64_t logical_block = 0;     // position inside the file, in blocks
    std::uint32_t length_blocks = 0;
    std::uint64_t physical_block = 0;    // position inside the filesystem, in blocks
    bool uninitialized = false;          // allocated but reads as zeros
};

// What we learned about one inode (the record that describes a file).
struct InodeInfo {
    bool read = false;                          // true once the inode bytes were decoded
    std::string limitation;                     // why something is missing, if it is
    InodeKind kind = InodeKind::unknown;
    std::optional<std::uint64_t> size_bytes;
    DataMapping mapping = DataMapping::unknown;
    std::optional<std::uint64_t> first_data_block;
    std::optional<std::uintmax_t> first_data_byte;   // relative to the image start
    std::vector<ExtentInfo> extents;                 // ext4 extent mapping, in file order
};

struct DirectoryEntryInfo {
    std::uint32_t inode_number = 0;
    std::string name;
    InodeInfo inode;                            // read == false for "." and ".."
};

struct DirectoryListing {
    bool attempted = false;
    std::string limitation;                     // e.g. "extent trees are not supported yet"
    std::vector<DirectoryEntryInfo> entries;
};

// What FileRelic can honestly say about one deleted directory entry. None of
// these is a promise of recovery: "likely_intact" means only that the blocks
// the inode still points at are not in use by anything else right now.
enum class DeletedStatus {
    unknown,                // could not be judged (see DeletedEntryInfo::note)
    name_only,              // the name survived, the inode number did not
    inode_in_use,           // the inode number now belongs to a live file
    empty_file,             // the file had no content, so there is nothing to recover
    block_map_cleared,      // the filesystem erased where the data was
    blocks_reused,          // all recorded data blocks are in use again
    blocks_partly_reused,   // some recorded data blocks are in use again
    likely_intact,          // name found, recorded blocks still free: data likely intact
};

// A directory entry that was unlinked but whose name is still on disk.
struct DeletedEntryInfo {
    std::string name;                           // raw on-disk name, made printable
    std::uint32_t inode_number = 0;             // 0 when the filesystem erased it
    InodeKind kind = InodeKind::unknown;        // from the inode, when still readable
    std::optional<std::uint64_t> size_bytes;    // size recorded in the inode, if readable
    std::optional<std::uint32_t> deleted_time;  // inode dtime, seconds since 1970 (UTC)
    DeletedStatus status = DeletedStatus::unknown;
    std::string note;                           // extra evidence, e.g. "2 of 5 blocks in use"
};

struct DeletedListing {
    bool attempted = false;
    std::string limitation;                     // why the scan may be incomplete, if it is
    std::vector<DeletedEntryInfo> entries;
};

// A deleted inode whose name is no longer reachable from any directory.
struct OrphanEntryInfo {
    std::uint32_t inode_number = 0;
    InodeKind kind = InodeKind::unknown;
    std::optional<std::uint64_t> size_bytes;
    std::optional<std::uint32_t> deleted_time;  // inode dtime, seconds since 1970 (UTC)
    std::string note;                           // why FileRelic flagged it as orphaned
};

struct OrphanListing {
    bool attempted = false;
    std::string limitation;                     // why the scan may be incomplete, if it is
    std::vector<OrphanEntryInfo> entries;
};

struct FileSystemInfo {
    FileSystemKind kind = FileSystemKind::unknown;
    std::string label = "unknown";
    std::string variant;                        // e.g. "ext4" (ext2/ext3/ext4 are told apart by features)
    bool signature_found = false;
    std::string volume_label;
    std::string uuid;
    std::uint32_t block_size_bytes = 0;
    std::uint16_t inode_size_bytes = 0;
    std::string state;
    std::string features;
    std::vector<std::string> limitations;       // what the reader could not do
    DirectoryListing root_directory;            // ext-family fixtures only, for now
};

struct PartitionInfo {
    PartitionTableKind table = PartitionTableKind::unknown;
    std::uint64_t number = 0;
    std::string name;
    std::uint32_t type_code = 0;
    std::string type_id;
    std::uint64_t start_sector = 0;
    std::uint64_t sector_count = 0;
    std::uintmax_t start_byte = 0;
    std::uintmax_t length_bytes = 0;
    bool range_valid = false;
    bool extended = false;
    bool logical = false;                       // MBR logical partition inside an extended one
    FileSystemInfo file_system;
    std::vector<std::string> warnings;
};

// How a section should be styled: a calm default, a positive result, something
// the user should look at, or something that went wrong.
enum class Severity { info, good, warning, problem };

// One "label: value" line of a section, already worded for humans.
struct ReportRow {
    std::string label;
    std::string value;
    bool technical = false;      // low-level detail (sector numbers, inode numbers, ...): shown on request
    std::string detail;          // extra technical text appended to the value when details are shown
};

// A block of the human-readable report. The core builds these ONCE from the typed
// results; the CLI prints them as text and the GUI draws them as cards. Neither
// interface re-words or re-parses anything.
struct ReportSection {
    ReportSectionKind kind = ReportSectionKind::diagnostic;
    Severity severity = Severity::info;
    std::string title;
    std::string summary;                        // one plain-English sentence (may be empty)
    std::vector<ReportRow> rows;
    std::vector<std::string> notes;             // bullet-point remarks
};

struct InspectionReport {
    std::filesystem::path image_path;
    std::uintmax_t image_size_bytes = 0;
    int exit_code = 1;
    PartitionTableKind partition_table = PartitionTableKind::unknown;
    ValidationState partition_table_state = ValidationState::unknown;
    std::vector<PartitionInfo> partitions;
    std::vector<std::string> warnings;
    std::vector<std::string> diagnostics;
    std::vector<ReportSection> sections;

    // Step-by-step record of what the scanner checked, for the CLI --verbose
    // flag and the GUI "Advanced" log. It is a log of events only: nothing
    // reads it back to discover results. All results live in the fields above.
    std::vector<std::string> trace;

    [[nodiscard]] bool succeeded() const { return exit_code == 0; }
};

// Opens an image read-only and returns its inspection result without choosing a UI.
InspectionReport inspect_image(const std::filesystem::path& image_path);

// Prints the human-readable report (the `sections`) as text. With `verbose`,
// technical rows (sector numbers, inode numbers, ...) and the scanner's step log
// are included too.
void render_report(const InspectionReport& report, std::ostream& output,
                   bool verbose = false);

// The scanner's step log as one block of text (used by the GUI Advanced view).
std::string render_trace(const InspectionReport& report);

} // namespace filerelic
