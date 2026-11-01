#pragma once

// A deliberately small ext2/3/4 metadata reader.
//
// It understands: the superblock, the first group descriptor, the root
// directory when it uses direct blocks, and inodes in the first inode group.
// It does NOT decode extent trees, htree directories, or deleted entries, and
// it does not recover any file. Those gaps are reported through `limitation`
// fields instead of being hidden.

#include <cstdint>

#include "scan_context.h"

namespace filerelic {

void read_ext_metadata(ScanContext& context,
                       std::uint64_t partition_start,
                       FileSystemInfo& file_system);

} // namespace filerelic
