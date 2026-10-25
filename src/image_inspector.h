#pragma once

#include <filesystem>

namespace filerelic {

// Opens an image read-only and writes a human-readable inspection report to stdout.
int inspect_image(const std::filesystem::path& image_path);

} // namespace filerelic
