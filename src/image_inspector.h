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

// What we learned about one inode (the record that describes a file).
struct InodeInfo {
    bool read = false;                          // true once the inode bytes were decoded
    std::string limitation;                     // why something is missing, if it is
    InodeKind kind = InodeKind::unknown;
    std::optional<std::uint64_t> size_bytes;
    DataMapping mapping = DataMapping::unknown;
    std::optional<std::uint32_t> first_data_block;
    std::optional<std::uintmax_t> first_data_byte;   // relative to the image start
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

struct FileSystemInfo {
    FileSystemKind kind = FileSystemKind::unknown;
    std::string label = "unknown";
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

struct ReportSection {
    ReportSectionKind kind = ReportSectionKind::diagnostic;
    std::string title;
    std::string details;
    bool has_warning = false;
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

// Writes the human-readable summary built from the typed fields.
// With include_trace the scanner's step log is appended.
void render_report(const InspectionReport& report, std::ostream& output,
                   bool include_trace = false);

// The scanner's step log as one block of text (used by the GUI Advanced view).
std::string render_trace(const InspectionReport& report);

} // namespace filerelic
