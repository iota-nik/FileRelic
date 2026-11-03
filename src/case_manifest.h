#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "image_inspector.h"

namespace filerelic {

struct CaseActionInfo {
    std::string operation;
    std::string source_path;
    std::string status;
    std::string output_path;
    std::optional<std::uint64_t> bytes_written;
    std::string sha256;
    std::string note;
};

struct CaseManifestOptions {
    std::string case_name;
    std::string examiner;
    std::string output_directory;
    std::vector<CaseActionInfo> actions;
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
