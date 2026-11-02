#include "report_format.h"

#include <cstdio>
#include <map>

namespace filerelic {

std::string format_count(std::uint64_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index != 0 && (digits.size() - index) % 3 == 0) out.push_back(',');
        out.push_back(digits[index]);
    }
    return out;
}

std::string format_size_short(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
    }
    static const char* const units[] = {"KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    return buffer;
}

std::string format_size(std::uint64_t bytes) {
    if (bytes < 1024) {
        return format_size_short(bytes);
    }
    return format_size_short(bytes) + " (" + format_count(bytes) + " bytes)";
}

std::string describe_mbr_type(std::uint32_t code) {
    static const std::map<std::uint32_t, std::string> names{
        {0x01, "FAT12"}, {0x04, "FAT16"}, {0x05, "Extended partition (container)"},
        {0x06, "FAT16"}, {0x07, "NTFS or exFAT (Windows)"}, {0x0b, "FAT32"}, {0x0c, "FAT32"},
        {0x0e, "FAT16"}, {0x0f, "Extended partition (container)"}, {0x82, "Linux swap"},
        {0x83, "Linux filesystem"}, {0x85, "Linux extended partition (container)"},
        {0x8e, "Linux LVM"}, {0xa5, "FreeBSD"}, {0xa6, "OpenBSD"}, {0xa9, "NetBSD"},
        {0xaf, "Apple HFS/HFS+"}, {0xee, "GPT protective partition"}, {0xef, "EFI system partition"},
        {0xfd, "Linux RAID"},
    };
    const auto found = names.find(code);
    if (found != names.end()) return found->second;
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "Unrecognized type (0x%02x)", code);
    return buffer;
}

std::string describe_gpt_type(const std::string& guid) {
    static const std::map<std::string, std::string> names{
        {"c12a7328-f81f-11d2-ba4b-00a0c93ec93b", "EFI system partition"},
        {"0fc63daf-8483-4772-8e79-3d69d8477de4", "Linux filesystem"},
        {"0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", "Linux swap"},
        {"e6d6d379-f507-44c2-a23c-238f2a3df928", "Linux LVM"},
        {"a19d880f-05fc-4d3b-a006-743f0f84911e", "Linux RAID"},
        {"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", "Microsoft basic data (NTFS, FAT, exFAT)"},
        {"e3c9e316-0b5c-4db8-817d-f92df00215ae", "Microsoft reserved"},
        {"de94bba4-06d1-4d40-a16a-bfd50179d6ac", "Windows recovery environment"},
        {"48465300-0000-11aa-aa11-00306543ecba", "Apple HFS+"},
        {"7c3457ef-0000-11aa-aa11-00306543ecba", "Apple APFS"},
        {"426f6f74-0000-11aa-aa11-00306543ecba", "Apple boot"},
        {"21686148-6449-6e6f-744e-656564454649", "BIOS boot partition"},
    };
    const auto found = names.find(guid);
    return found != names.end() ? found->second : "Unrecognized type (" + guid + ")";
}

std::string describe_file_system(const FileSystemInfo& file_system) {
    switch (file_system.kind) {
    case FileSystemKind::ext_family:
        return file_system.variant.empty() ? "ext2/ext3/ext4" : file_system.variant;
    case FileSystemKind::ntfs: return "NTFS (Windows)";
    case FileSystemKind::apfs: return "APFS (Apple)";
    case FileSystemKind::xfs: return "XFS (Linux)";
    case FileSystemKind::btrfs: return "Btrfs (Linux)";
    case FileSystemKind::unknown: break;
    }
    return "no recognized file system";
}

bool can_read_files(const FileSystemInfo& file_system) {
    return file_system.kind == FileSystemKind::ext_family;
}

std::string describe_inode_kind(InodeKind kind) {
    switch (kind) {
    case InodeKind::regular_file: return "file";
    case InodeKind::directory: return "folder";
    case InodeKind::symbolic_link: return "link";
    case InodeKind::other: return "special item";
    case InodeKind::unknown: break;
    }
    return "unknown";
}

std::string describe_validation(ValidationState state) {
    switch (state) {
    case ValidationState::valid: return "Valid - every entry checked out";
    case ValidationState::warning: return "Usable, but some entries have problems";
    case ValidationState::invalid: return "Not valid - the table is damaged or inconsistent";
    case ValidationState::unknown: break;
    }
    return "Not detected";
}

} // namespace filerelic
