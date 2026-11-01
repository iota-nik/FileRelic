#include "filesystem_probe.h"

#include <string_view>

#include "ext_reader.h"

namespace filerelic {
namespace {

bool matches_at(ReadOnlySource& source, std::uint64_t offset, std::string_view expected) {
    const ReadResult actual = source.read(offset, expected.size());
    if (!actual.ok()) {
        return false;
    }
    // Compare as unsigned bytes: plain `char` is signed on many platforms, so a
    // byte such as 0xef would otherwise never equal itself.
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual.bytes[index] != static_cast<std::uint8_t>(expected[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

FileSystemInfo detect_file_system(ReadOnlySource& source, std::uint64_t partition_start) {
    FileSystemInfo info;
    const auto found = [&info](FileSystemKind kind, const char* label) {
        info.kind = kind;
        info.label = label;
        info.signature_found = true;
    };

    if (matches_at(source, partition_start + 3, "NTFS    ")) {
        found(FileSystemKind::ntfs, "probable NTFS (boot-sector signature)");
    } else if (matches_at(source, partition_start + 32, "NXSB")) {
        found(FileSystemKind::apfs, "probable APFS container (NXSB signature)");
    } else if (matches_at(source, partition_start, "XFSB")) {
        found(FileSystemKind::xfs, "probable XFS (superblock signature)");
    } else if (matches_at(source, partition_start + 1024 + 56, std::string_view("\x53\xef", 2))) {
        found(FileSystemKind::ext_family, "probable ext-family filesystem (ext2/ext3/ext4 magic)");
    } else if (matches_at(source, partition_start + 0x10040, "_BHRfS_M")) {
        found(FileSystemKind::btrfs, "probable Btrfs (superblock signature)");
    }
    return info;
}

FileSystemInfo inspect_file_system(ScanContext& context, std::uint64_t partition_start) {
    FileSystemInfo file_system = detect_file_system(context.source, partition_start);
    if (file_system.signature_found) {
        context.trace("  File system: " + file_system.label);
    } else {
        file_system.label = "no supported signature found";
        context.trace("  File system: no supported signature found");
    }
    if (file_system.kind == FileSystemKind::ext_family) {
        read_ext_metadata(context, partition_start, file_system);
    }
    return file_system;
}

} // namespace filerelic
