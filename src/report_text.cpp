// Draws InspectionReport::sections as plain text for the terminal.
// All wording lives in report_sections.cpp; this file only does layout.

#include <algorithm>
#include <ostream>

#include "image_inspector.h"

namespace filerelic {
namespace {

const char* severity_tag(Severity severity) {
    switch (severity) {
    case Severity::good: return "  [OK]";
    case Severity::warning: return "  [WARNING]";
    case Severity::problem: return "  [PROBLEM]";
    case Severity::info: break;
    }
    return "";
}

} // namespace

void render_report(const InspectionReport& report, std::ostream& output, bool verbose) {
    output << "FileRelic report\n"
           << "(read-only inspection - the image was not modified)\n";

    for (const ReportSection& section : report.sections) {
        output << "\n== " << section.title << severity_tag(section.severity) << " ==\n";
        if (!section.summary.empty()) {
            output << "  " << section.summary << '\n';
        }

        std::size_t width = 0;
        for (const ReportRow& row : section.rows) {
            if (!row.technical || verbose) width = std::max(width, row.label.size());
        }
        for (const ReportRow& row : section.rows) {
            if (row.technical && !verbose) continue;
            output << "  " << row.label << ':' << std::string(width - row.label.size() + 2, ' ') << row.value;
            if (verbose && !row.detail.empty()) output << "  (" << row.detail << ')';
            output << '\n';
        }
        for (const std::string& note : section.notes) {
            output << "  - " << note << '\n';
        }
    }

    if (verbose && !report.trace.empty()) {
        output << "\n== Step-by-step trace ==\n";
        for (const std::string& line : report.trace) output << "  " << line << '\n';
    }
}

} // namespace filerelic
