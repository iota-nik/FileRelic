// Orchestration and presentation for the inspection core.
//
//   image_source.*       read-only bounded access to the image bytes
//   partition_scanner.*  MBR / extended MBR / GPT discovery
//   filesystem_probe.*   filesystem signature detection
//   ext_reader.*         small ext2/3/4 metadata reader
//   report_sections.*    plain-language report sections (one wording for CLI and GUI)
//   report_text.cpp      draws the sections as terminal text
//   image_inspector.*    (this file) glue: run the scan, then build the report sections

#include "image_inspector.h"

#include <iomanip>
#include <sstream>

#include "image_source.h"
#include "partition_scanner.h"
#include "report_sections.h"
#include "scan_context.h"

namespace filerelic {
namespace {
std::string join_lines(const std::vector<std::string>& lines) {
    std::string joined;
    for (const std::string& line : lines) {
        if (!joined.empty()) joined.push_back('\n');
        joined += line;
    }
    return joined;
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

} // namespace filerelic
