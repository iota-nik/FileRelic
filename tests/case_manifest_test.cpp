#include <cassert>
#include <string>

#include "case_manifest.h"

int main() {
    filerelic::InspectionReport report;
    report.exit_code = 0;

    filerelic::CaseManifestOptions options;
    options.actions.push_back({
        "extract",
        "/docs/a.txt",
        "complete",
        "/output/a.txt",
        42,
        "0123456789abcdef",
        "read successfully",
    });
    options.actions.push_back({
        "extract",
        "/docs",
        "complete",
        "/output/docs",
        std::nullopt,
        "",
        "directory totals are not aggregated",
    });

    const std::string json = filerelic::render_case_manifest_json(report, options);
    assert(json.find("\"actions\": [{") != std::string::npos);
    assert(json.find("\"source_path\":\"/docs/a.txt\"") != std::string::npos);
    assert(json.find("\"bytes_written\":42") != std::string::npos);
    assert(json.find("\"sha256\":\"0123456789abcdef\"") != std::string::npos);
    assert(json.find("\"bytes_written\":null") != std::string::npos);
    assert(json.find("\"note\":\"directory totals are not aggregated\"") != std::string::npos);
}
