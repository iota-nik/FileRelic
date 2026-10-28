#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace filerelic {

enum class ReportSectionKind {
    image,
    partition_layout,
    partition,
    warnings,
    diagnostic,
};

enum class PartitionTableKind { unknown, mbr, gpt };
enum class FileSystemKind { unknown, ntfs, apfs, xfs, ext_family, btrfs };
enum class ValidationState { unknown, valid, warning, invalid };

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
    FileSystemInfo file_system;
    std::vector<std::string> warnings;
};

struct ReportSection {
    ReportSectionKind kind = ReportSectionKind::diagnostic;
    std::string title;
    std::string details;
    bool has_warning = false;
};

// Shared result returned by the inspection core. CLI and GUI render this
// object independently; neither interface needs to parse the other's output.
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

    // Detailed trace retained for the CLI appendix and GUI's optional raw log.
    // Structured fields above are the primary source for interface rendering.
    std::string rendered_text;

    [[nodiscard]] bool succeeded() const { return exit_code == 0; }
};

// Opens an image read-only and returns its inspection result without choosing a UI.
InspectionReport inspect_image(const std::filesystem::path& image_path);

// Renders the compatible human-readable report for the CLI.
void render_report(const InspectionReport& report, std::ostream& output);

} // namespace filerelic
