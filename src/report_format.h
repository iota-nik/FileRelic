#pragma once

// Plain-language wording helpers shared by everything that shows results to a
// person (the report sections, the `ls` command, the GUI). One place, one wording.

#include <cstdint>
#include <string>

#include "image_inspector.h"

namespace filerelic {

// 1048576 -> "1,048,576"
std::string format_count(std::uint64_t value);
// 35 -> "35 bytes";  1048576 -> "1.0 MiB"
std::string format_size_short(std::uint64_t bytes);
// 1048576 -> "1.0 MiB (1,048,576 bytes)";  35 -> "35 bytes"
std::string format_size(std::uint64_t bytes);

// MBR type code -> "Linux filesystem", "NTFS or exFAT (Windows)", ...
std::string describe_mbr_type(std::uint32_t code);
// GPT type GUID (lowercase, dashed) -> "EFI system partition", "Linux filesystem", ...
std::string describe_gpt_type(const std::string& guid);
// "ext4", "NTFS (Windows)", ... or "no recognized file system"
std::string describe_file_system(const FileSystemInfo& file_system);
// Can FileRelic list and extract files from this file system today?
bool can_read_files(const FileSystemInfo& file_system);

// "file", "folder", "link", "other"
std::string describe_inode_kind(InodeKind kind);

// One honest sentence for a deleted entry's status, e.g.
// "name found, data likely intact". Never promises recovery.
std::string describe_deleted_status(DeletedStatus status);
// 1700000000 -> "2023-11-14 22:13:20 UTC"
std::string format_unix_time_utc(std::uint32_t seconds);

// Wording for the state of a partition table.
std::string describe_validation(ValidationState state);

} // namespace filerelic
