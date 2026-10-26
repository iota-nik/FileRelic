#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace filerelic {

// The canonical result of a read-only image inspection. Interfaces render this
// object; they do not run their own parsing logic.
struct InspectionReport {
    std::filesystem::path image_path;
    std::uintmax_t image_size_bytes = 0;
    int exit_code = 1;
    std::string rendered_text;

    [[nodiscard]] bool succeeded() const { return exit_code == 0; }
};

// Opens an image read-only and returns its inspection result without choosing a UI.
InspectionReport inspect_image(const std::filesystem::path& image_path);

// Renders the current human-readable form of a report for a terminal or UI.
void render_report(const InspectionReport& report, std::ostream& output);

} // namespace filerelic
