#pragma once

#include <filesystem>
#include <string>

#include "image_inspector.h"

namespace filerelic {

struct CaseManifestOptions {
    std::string case_name;
    std::string examiner;
    std::string output_directory;
};

// Builds a JSON manifest from the typed inspection result. It is intentionally
// small and explicit: the manifest records the filesystem facts we already know,
// not a second interpretation layer that could drift from the core report.
std::string render_case_manifest_json(const InspectionReport& report,
                                     const CaseManifestOptions& options = {});

// Writes the manifest to a user-chosen path. Returns true when the file was
// written; false when the output path could not be created.
bool write_case_manifest_json(const InspectionReport& report,
                             const std::filesystem::path& destination,
                             const CaseManifestOptions& options = {});

// A plain-English report header intended for the CLI and GUI export workflows.
std::string render_case_report_text(const InspectionReport& report,
                                   const CaseManifestOptions& options = {});

} // namespace filerelic
