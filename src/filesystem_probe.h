#pragma once

// Filesystem identification for a byte range inside an image.
//
// A signature match ("probable NTFS") is evidence, not proof, and says nothing
// about whether FileRelic can read the files inside.

#include <cstdint>

#include "scan_context.h"

namespace filerelic {

// Looks for known signatures at the expected offsets. Never reads past the image.
FileSystemInfo detect_file_system(ReadOnlySource& source, std::uint64_t partition_start);

// detect_file_system() plus, for ext-family, the metadata reader. Logs steps to
// the context trace and returns the typed result.
FileSystemInfo inspect_file_system(ScanContext& context, std::uint64_t partition_start);

} // namespace filerelic
