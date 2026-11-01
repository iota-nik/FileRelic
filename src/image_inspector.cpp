// Orchestration and presentation for the inspection core.
//
//   image_source.*       read-only bounded access to the image bytes
//   partition_scanner.*  MBR / extended MBR / GPT discovery
//   filesystem_probe.*   filesystem signature detection
//   ext_reader.*         small ext2/3/4 metadata reader
//   image_inspector.*    (this file) glue: run the scan, build report sections,
//                        render text for the CLI

#include "image_inspector.h"

#include <iomanip>
#include <ostream>
#include <sstream>

#include "image_source.h"
#include "partition_scanner.h"
#include "scan_context.h"

namespace filerelic {
namespace {

const char* table_name(PartitionTableKind table) {
    switch (table) {
    case PartitionTableKind::mbr: return "MBR";
    case PartitionTableKind::gpt: return "GPT";
    case PartitionTableKind::unknown: return "Unknown";
    }
    return "Unknown";
}

const char* state_name(ValidationState state) {
    switch (state) {
    case ValidationState::valid: return "validated";
    case ValidationState::warning: return "has warnings";
    case ValidationState::invalid: return "invalid";
    case ValidationState::unknown: return "not detected";
    }
    return "unknown";
}

const char* inode_kind_name(InodeKind kind) {
    switch (kind) {
    case InodeKind::regular_file: return "regular file";
    case InodeKind::directory: return "directory";
    case InodeKind::symbolic_link: return "symbolic link";
    case InodeKind::other: return "other inode type";
    case InodeKind::unknown: return "unknown type";
    }
    return "unknown type";
}

std::string partition_title(const PartitionInfo& partition) {
    std::string title = table_name(partition.table);
    title += partition.logical ? " logical partition " : " partition ";
    title += std::to_string(partition.number);
    if (!partition.name.empty()) {
        title += " (" + partition.name + ")";
    }
    return title;
}

// One line describing a directory entry, shared by the CLI and the GUI cards.
std::string describe_entry(const DirectoryEntryInfo& entry) {
    std::string text = entry.name + " (inode " + std::to_string(entry.inode_number) + ")";
    const InodeInfo& inode = entry.inode;
    if (!inode.read && inode.limitation.empty()) {
        return text;                                  // "." and ".." are not decoded
    }
    std::vector<std::string> parts;
    if (inode.read) {
        parts.push_back(inode_kind_name(inode.kind));
        if (inode.size_bytes) parts.push_back(std::to_string(*inode.size_bytes) + " bytes");
        if (inode.first_data_block) {
            parts.push_back("first data block " + std::to_string(*inode.first_data_block)
                            + " (byte " + std::to_string(*inode.first_data_byte) + ")");
        }
    }
    if (!inode.limitation.empty()) parts.push_back("limitation: " + inode.limitation);
    text += ": ";
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) text += ", ";
        text += parts[index];
    }
    return text;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string joined;
    for (const std::string& line : lines) {
        if (!joined.empty()) joined.push_back('\n');
        joined += line;
    }
    return joined;
}

std::string describe_directory(const DirectoryListing& listing) {
    std::vector<std::string> lines;
    for (const DirectoryEntryInfo& entry : listing.entries) lines.push_back(describe_entry(entry));
    if (!listing.limitation.empty()) lines.push_back("Limitation: " + listing.limitation);
    return join_lines(lines);
}

void build_sections(InspectionReport& report) {
    report.sections.push_back({ReportSectionKind::image, "Image",
                               report.image_path.string() + "\n"
                                   + std::to_string(report.image_size_bytes) + " bytes",
                               false});

    if (report.partition_table != PartitionTableKind::unknown) {
        report.sections.push_back({
            ReportSectionKind::partition_layout, "Partition layout",
            std::string(table_name(report.partition_table)) + " partition table "
                + state_name(report.partition_table_state),
            report.partition_table_state == ValidationState::warning
                || report.partition_table_state == ValidationState::invalid});
    }

    for (const PartitionInfo& partition : report.partitions) {
        std::vector<std::string> lines;
        if (!partition.type_id.empty()) lines.push_back("Type: " + partition.type_id);
        lines.push_back("Starts: sector " + std::to_string(partition.start_sector)
                        + " (byte " + std::to_string(partition.start_byte) + ")");
        lines.push_back("Length: " + std::to_string(partition.sector_count) + " sectors ("
                        + std::to_string(partition.length_bytes) + " bytes)");
        lines.push_back(std::string("Status: ")
                        + (partition.range_valid ? "within image bounds" : "invalid range"));
        if (partition.extended) lines.push_back("Contains an extended MBR logical-partition chain");

        const FileSystemInfo& fs = partition.file_system;
        if (fs.signature_found) {
            lines.push_back("File system: " + fs.label);
        } else if (!partition.extended && partition.range_valid) {
            lines.push_back("File system: no supported signature found");
        }
        if (!fs.volume_label.empty()) lines.push_back("Volume label: " + fs.volume_label);
        if (!fs.uuid.empty()) lines.push_back("UUID: " + fs.uuid);
        if (fs.block_size_bytes != 0) lines.push_back("Block size: " + std::to_string(fs.block_size_bytes) + " bytes");
        if (fs.inode_size_bytes != 0) lines.push_back("Inode size: " + std::to_string(fs.inode_size_bytes) + " bytes");
        if (!fs.state.empty()) lines.push_back("Filesystem state: " + fs.state);
        if (!fs.features.empty()) lines.push_back("Features: " + fs.features);
        for (const std::string& limitation : fs.limitations) lines.push_back("Limitation: " + limitation);
        for (const std::string& warning : partition.warnings) lines.push_back("Warning: " + warning);

        report.sections.push_back({ReportSectionKind::partition, partition_title(partition),
                                   join_lines(lines),
                                   !partition.warnings.empty() || !partition.range_valid});

        if (fs.root_directory.attempted) {
            report.sections.push_back({ReportSectionKind::directory_listing,
                                       "Root directory of " + partition_title(partition),
                                       describe_directory(fs.root_directory),
                                       !fs.root_directory.limitation.empty()});
        }
    }

    if (!report.warnings.empty()) {
        report.sections.push_back({ReportSectionKind::warnings, "Warnings",
                                   join_lines(report.warnings), true});
    }
    if (!report.diagnostics.empty()) {
        report.sections.push_back({ReportSectionKind::diagnostic, "Diagnostic",
                                   join_lines(report.diagnostics), !report.succeeded()});
    }
}

} // namespace

InspectionReport inspect_image(const std::filesystem::path& image_path) {
    InspectionReport report{};
    report.image_path = image_path;

    std::string error;
    std::unique_ptr<FileImageSource> source = FileImageSource::open(image_path, error);
    if (!source) {
        report.diagnostics.push_back(error);
        report.trace.push_back("Error: " + error + ": " + image_path.string());
        report.exit_code = 1;
        build_sections(report);
        return report;
    }

    report.image_size_bytes = source->size();
    ScanContext context{*source, report};
    context.trace("Image file: " + source->description());
    context.trace("Size: " + std::to_string(source->size()) + " bytes");

    const std::uint64_t preview_length = std::min<std::uint64_t>(source->size(), 16);
    const ReadResult preview = source->read(0, preview_length);
    if (!preview.ok()) {
        report.diagnostics.push_back("Image changed or could not be read completely during inspection");
        context.trace("Error: " + *preview.error);
        report.exit_code = 1;
        build_sections(report);
        return report;
    }
    std::ostringstream hex;
    for (const std::uint8_t value : preview.bytes) {
        hex << ' ' << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(value);
    }
    context.trace("First " + std::to_string(preview_length) + " bytes (hex):" + hex.str());

    scan_partitions(context);
    report.exit_code = 0;
    build_sections(report);
    return report;
}

std::string render_trace(const InspectionReport& report) {
    return join_lines(report.trace);
}

void render_report(const InspectionReport& report, std::ostream& output, bool include_trace) {
    output << "FileRelic inspection report\n"
           << "Image: " << report.image_path.string() << '\n'
           << "Size: " << report.image_size_bytes << " bytes\n";
    if (report.partition_table == PartitionTableKind::unknown) {
        output << "Partition table: not identified\n";
    } else {
        output << "Partition table: " << table_name(report.partition_table)
               << " (" << state_name(report.partition_table_state) << ")\n";
    }

    if (!report.partitions.empty()) {
        output << "\nPartitions:\n";
        for (const PartitionInfo& partition : report.partitions) {
            output << "  " << partition_title(partition) << ":\n"
                   << "    Type: " << (partition.type_id.empty() ? "unknown" : partition.type_id) << '\n'
                   << "    Start: sector " << partition.start_sector << " (byte "
                   << partition.start_byte << ")\n"
                   << "    Length: " << partition.sector_count << " sectors ("
                   << partition.length_bytes << " bytes)\n"
                   << "    Range: " << (partition.range_valid ? "within image bounds" : "invalid") << '\n';
            if (partition.extended) {
                output << "    Role: extended partition container\n";
            }
            const FileSystemInfo& fs = partition.file_system;
            if (!partition.extended && partition.range_valid) {
                output << "    File system: " << fs.label << '\n';
            } else if (!partition.range_valid) {
                output << "    File system: not inspected because the partition range is invalid\n";
            }
            if (!fs.volume_label.empty()) output << "    Volume label: " << fs.volume_label << '\n';
            if (!fs.uuid.empty()) output << "    UUID: " << fs.uuid << '\n';
            if (fs.block_size_bytes != 0) output << "    Block size: " << fs.block_size_bytes << " bytes\n";
            if (fs.inode_size_bytes != 0) output << "    Inode size: " << fs.inode_size_bytes << " bytes\n";
            if (!fs.state.empty()) output << "    Filesystem state: " << fs.state << '\n';
            if (!fs.features.empty()) output << "    Features: " << fs.features << '\n';
            for (const std::string& limitation : fs.limitations) {
                output << "    Limitation: " << limitation << '\n';
            }
            if (fs.root_directory.attempted) {
                output << "    Root directory:\n";
                for (const DirectoryEntryInfo& entry : fs.root_directory.entries) {
                    output << "      " << describe_entry(entry) << '\n';
                }
                if (!fs.root_directory.limitation.empty()) {
                    output << "      Limitation: " << fs.root_directory.limitation << '\n';
                }
            }
        }
    }

    if (!report.warnings.empty()) {
        output << "\nWarnings:\n";
        for (const std::string& warning : report.warnings) output << "  - " << warning << '\n';
    }
    if (!report.diagnostics.empty()) {
        output << "\nDiagnostics:\n";
        for (const std::string& diagnostic : report.diagnostics) output << "  - " << diagnostic << '\n';
    }

    if (include_trace && !report.trace.empty()) {
        output << "\nInspection trace (--verbose):\n";
        for (const std::string& line : report.trace) output << "  " << line << '\n';
    }
}

} // namespace filerelic
