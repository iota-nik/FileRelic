#include "report_sections.h"

#include "report_format.h"

namespace filerelic {
namespace {

constexpr std::size_t max_listed_items = 20;

const char* table_name(PartitionTableKind table) {
    switch (table) {
    case PartitionTableKind::mbr: return "MBR";
    case PartitionTableKind::gpt: return "GPT";
    case PartitionTableKind::unknown: break;
    }
    return "none";
}

std::string plural(std::size_t count, const std::string& one, const std::string& many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

std::string file_name_of(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name.empty() ? path.string() : name;
}

std::string partition_type_text(const PartitionInfo& partition) {
    return partition.table == PartitionTableKind::gpt ? describe_gpt_type(partition.type_id)
                                                      : describe_mbr_type(partition.type_code);
}

std::string partition_label(const PartitionInfo& partition) {
    std::string label = "Partition " + std::to_string(partition.number);
    if (partition.logical) label += " (logical)";
    return label;
}

// "ext4 file system named "X"" / "looks like NTFS" / ...
std::string describe_contents(const PartitionInfo& partition) {
    const FileSystemInfo& fs = partition.file_system;
    if (partition.extended) return "a container for logical partitions";
    if (!partition.range_valid) return "could not be checked (its range is invalid)";
    if (!fs.signature_found) return "no recognized file system";
    std::string text = describe_file_system(fs);
    if (fs.kind == FileSystemKind::ext_family) {
        text += " file system";
        if (!fs.volume_label.empty() && fs.volume_label != "(none)") text += " named \"" + fs.volume_label + "\"";
        text += " - files can be listed and extracted";
    } else {
        text += " (recognized by its signature only - FileRelic cannot read its files yet)";
    }
    return text;
}

Severity severity_of(ValidationState state) {
    switch (state) {
    case ValidationState::valid: return Severity::good;
    case ValidationState::warning: return Severity::warning;
    case ValidationState::invalid: return Severity::problem;
    case ValidationState::unknown: break;
    }
    return Severity::info;
}

ReportSection summary_section(const InspectionReport& report) {
    ReportSection section;
    section.kind = ReportSectionKind::summary;
    section.title = "Summary";

    if (!report.succeeded()) {
        section.severity = Severity::problem;
        section.notes.push_back("FileRelic could not read this image"
                                + (report.diagnostics.empty() ? std::string() : ": " + report.diagnostics.front()) + ".");
        return section;
    }

    if (report.partition_table == PartitionTableKind::unknown) {
        section.severity = Severity::warning;
        section.notes.push_back("No MBR or GPT partition table was found at the start of the image.");
        section.notes.push_back("Images that hold a file system directly, with no partition table, are not supported yet.");
        return section;
    }

    section.severity = severity_of(report.partition_table_state);
    section.notes.push_back(std::string("The image has ") + (report.partition_table == PartitionTableKind::mbr ? "an " : "a ") + table_name(report.partition_table)
                            + " partition table with " + plural(report.partitions.size(), "partition", "partitions")
                            + (report.partition_table_state == ValidationState::valid ? ", all valid." : "."));
    for (const PartitionInfo& partition : report.partitions) {
        if (partition.extended) continue;
        section.notes.push_back(partition_label(partition) + " (" + partition_type_text(partition) + ", "
                                + (partition.range_valid ? format_size_short(partition.length_bytes) : "size unreliable")
                                + "): " + describe_contents(partition) + ".");
    }
    if (!report.warnings.empty()) {
        section.notes.push_back(plural(report.warnings.size(), "warning", "warnings") + " - see below.");
    }
    return section;
}

ReportSection image_section(const InspectionReport& report) {
    ReportSection section;
    section.kind = ReportSectionKind::image;
    section.title = "Image file";
    section.rows.push_back({"Name", file_name_of(report.image_path), false, ""});
    section.rows.push_back({"Location", report.image_path.string(), false, ""});
    section.rows.push_back({"Size", format_size(report.image_size_bytes), false, ""});
    section.rows.push_back({"Access", "Read-only (never modified)", false, ""});
    return section;
}

ReportSection layout_section(const InspectionReport& report) {
    ReportSection section;
    section.kind = ReportSectionKind::partition_layout;
    section.title = "Partition table";
    section.severity = severity_of(report.partition_table_state);
    const bool gpt = report.partition_table == PartitionTableKind::gpt;
    section.rows.push_back({"Kind", gpt ? "GPT - the modern GUID partition table"
                                        : "MBR - the classic PC partition table", false, ""});
    section.rows.push_back({"Status", describe_validation(report.partition_table_state), false, ""});
    section.rows.push_back({"Partitions", std::to_string(report.partitions.size()), false, ""});
    return section;
}

ReportSection partition_section(const PartitionInfo& partition) {
    ReportSection section;
    section.kind = ReportSectionKind::partition;
    section.title = partition_label(partition);
    if (!partition.name.empty()) section.title += " \"" + partition.name + "\"";
    section.title += " - " + partition_type_text(partition);
    section.severity = (!partition.warnings.empty() || !partition.range_valid) ? Severity::warning : Severity::info;

    const FileSystemInfo& fs = partition.file_system;
    if (partition.range_valid) {
        section.rows.push_back({"Starts at", format_size_short(partition.start_byte) + " into the image", false,
                                "sector " + format_count(partition.start_sector)});
        section.rows.push_back({"Size", format_size(partition.length_bytes), false,
                                format_count(partition.sector_count) + " sectors"});
        section.rows.push_back({"Fits in the image", "Yes", false, ""});
    } else {
        section.rows.push_back({"Fits in the image", "No - it extends past the end of the image", false, ""});
        section.rows.push_back({"Start sector", format_count(partition.start_sector), true, ""});
        section.rows.push_back({"Claimed size", format_count(partition.sector_count) + " sectors", true, ""});
    }
    section.rows.push_back({"Contents", describe_contents(partition), false, ""});

    if (fs.kind == FileSystemKind::ext_family) {
        if (!fs.volume_label.empty()) section.rows.push_back({"Volume name", fs.volume_label, false, ""});
        if (!fs.state.empty()) {
            const std::string shutdown = fs.state == "cleanly unmounted" ? "Clean (shut down properly)"
                : fs.state == "errors recorded" ? "Errors were recorded" : "Unknown";
            section.rows.push_back({"Last shutdown", shutdown, false, ""});
        }
        if (!fs.features.empty()) section.rows.push_back({"Features", fs.features, true, ""});
        if (!fs.uuid.empty()) section.rows.push_back({"Volume ID", fs.uuid, true, ""});
        if (fs.block_size_bytes != 0) section.rows.push_back({"Block size", format_size_short(fs.block_size_bytes), true, ""});
        if (fs.inode_size_bytes != 0) section.rows.push_back({"Inode size", format_size_short(fs.inode_size_bytes), true, ""});
    }
    for (const std::string& limitation : fs.limitations) section.notes.push_back("Limitation: " + limitation);
    for (const std::string& warning : partition.warnings) section.notes.push_back(warning);
    return section;
}

ReportSection listing_section(const PartitionInfo& partition) {
    const DirectoryListing& listing = partition.file_system.root_directory;
    ReportSection section;
    section.kind = ReportSectionKind::directory_listing;
    section.title = "Top-level files and folders - " + partition_label(partition);
    section.severity = listing.limitation.empty() ? Severity::info : Severity::warning;

    std::size_t total = 0;
    for (const DirectoryEntryInfo& entry : listing.entries) {
        if (entry.name == "." || entry.name == "..") continue;
        ++total;
        if (total > max_listed_items) continue;

        const InodeInfo& inode = entry.inode;
        std::string value = describe_inode_kind(inode.kind);
        if (inode.size_bytes && inode.kind == InodeKind::regular_file) value += ", " + format_size_short(*inode.size_bytes);
        std::string detail = "inode " + std::to_string(entry.inode_number);
        if (inode.mapping == DataMapping::extent_tree) {
            detail += ", " + plural(inode.extents.size(), "extent", "extents");
        }
        if (inode.first_data_block) detail += ", first block " + format_count(*inode.first_data_block);
        if (!inode.limitation.empty()) value += " (note: " + inode.limitation + ")";
        section.rows.push_back({entry.name + (inode.kind == InodeKind::directory ? "/" : ""), value, false, detail});
    }
    section.summary = total == 0 ? "This folder is empty." : plural(total, "item", "items") + " at the top level.";
    if (total > max_listed_items) {
        section.notes.push_back("...and " + std::to_string(total - max_listed_items)
                                + " more. Use the `filerelic ls` command to see them all.");
    }
    if (!listing.limitation.empty()) section.notes.push_back("Limitation: " + listing.limitation);
    return section;
}

} // namespace

void build_sections(InspectionReport& report) {
    report.sections.clear();
    report.sections.push_back(summary_section(report));
    report.sections.push_back(image_section(report));

    if (report.succeeded() && report.partition_table != PartitionTableKind::unknown) {
        report.sections.push_back(layout_section(report));
    }
    for (const PartitionInfo& partition : report.partitions) {
        report.sections.push_back(partition_section(partition));
        if (partition.file_system.root_directory.attempted) {
            report.sections.push_back(listing_section(partition));
        }
    }

    if (!report.warnings.empty()) {
        ReportSection warnings;
        warnings.kind = ReportSectionKind::warnings;
        warnings.severity = Severity::warning;
        warnings.title = "Warnings";
        warnings.summary = "These things look wrong or unusual in the image:";
        warnings.notes = report.warnings;
        report.sections.push_back(std::move(warnings));
    }
    if (!report.diagnostics.empty() && !report.succeeded()) {
        ReportSection problem;
        problem.kind = ReportSectionKind::diagnostic;
        problem.severity = Severity::problem;
        problem.title = "What went wrong";
        problem.notes = report.diagnostics;
        report.sections.push_back(std::move(problem));
    } else if (!report.diagnostics.empty()) {
        ReportSection remarks;
        remarks.kind = ReportSectionKind::diagnostic;
        remarks.severity = Severity::info;
        remarks.title = "Remarks";
        remarks.notes = report.diagnostics;
        report.sections.push_back(std::move(remarks));
    }
}

} // namespace filerelic
