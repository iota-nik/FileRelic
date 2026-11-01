#include "ext_reader.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "byte_utils.h"

namespace filerelic {
namespace {

using bytes::le16;
using bytes::le32;

constexpr std::uint32_t root_inode_number = 2;
constexpr std::uint32_t extents_flag = 0x00080000U;
constexpr std::size_t max_directory_entries = 64;

struct ExtGeometry {
    std::uint32_t block_size;
    std::uint16_t inode_size;
    std::uint32_t inodes_per_group;
};

std::string read_ascii_field(const std::uint8_t* data, std::size_t length) {
    std::string value;
    for (std::size_t index = 0; index < length && data[index] != 0; ++index) {
        const std::uint8_t character = data[index];
        value.push_back(character >= 32 && character <= 126 ? static_cast<char>(character) : '?');
    }
    return value.empty() ? "(none)" : value;
}

std::string format_uuid(const std::uint8_t* uuid) {
    std::ostringstream formatted;
    for (std::size_t index = 0; index < 16; ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            formatted << '-';
        }
        formatted << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(uuid[index]);
    }
    return formatted.str();
}

// Decodes one inode from the first inode group.
InodeInfo read_inode(ScanContext& context,
                     std::uint64_t partition_start,
                     std::uint32_t inode_table_block,
                     const ExtGeometry& geometry,
                     std::uint32_t inode_number) {
    InodeInfo info;
    // Full ext4 support must pick the right group descriptor for larger inode
    // numbers; this first reader only handles the first group.
    if (inode_number == 0 || inode_number > geometry.inodes_per_group) {
        info.limitation = "inode is outside the first inode group (not supported yet)";
        return info;
    }

    const std::uint64_t inode_offset = partition_start
        + static_cast<std::uint64_t>(inode_table_block) * geometry.block_size
        + static_cast<std::uint64_t>(inode_number - 1) * geometry.inode_size;
    const ReadResult raw = context.source.read(inode_offset, geometry.inode_size);
    if (!raw.ok()) {
        info.limitation = "could not read inode " + std::to_string(inode_number);
        return info;
    }
    const std::uint8_t* inode = raw.bytes.data();

    const std::uint16_t mode = le16(inode);
    const std::uint32_t size_low = le32(inode + 4);
    const std::uint32_t flags = le32(inode + 32);
    const std::uint16_t type = mode & 0xf000U;

    info.read = true;
    if (type == 0x8000U) {
        info.kind = InodeKind::regular_file;
    } else if (type == 0x4000U) {
        info.kind = InodeKind::directory;
    } else if (type == 0xa000U) {
        info.kind = InodeKind::symbolic_link;
    } else {
        info.kind = InodeKind::other;
        info.limitation = "unsupported inode type";
        return info;
    }

    std::uint64_t size = size_low;
    if (info.kind == InodeKind::regular_file && geometry.inode_size >= 112) {
        size |= static_cast<std::uint64_t>(le32(inode + 108)) << 32U;   // i_size_high
    }
    info.size_bytes = size;

    if ((flags & extents_flag) != 0) {
        info.mapping = DataMapping::extent_tree;
        info.limitation = "data is stored in an extent tree, which is not decoded yet";
        return info;
    }

    const std::uint32_t first_block = le32(inode + 40);
    if (first_block == 0) {
        info.mapping = DataMapping::none;
        info.limitation = "no first direct data block";
        return info;
    }
    info.mapping = DataMapping::direct_blocks;
    info.first_data_block = first_block;
    info.first_data_byte = static_cast<std::uintmax_t>(partition_start)
        + static_cast<std::uintmax_t>(first_block) * geometry.block_size;
    return info;
}

void read_root_directory(ScanContext& context,
                         std::uint64_t partition_start,
                         const ExtGeometry& geometry,
                         DirectoryListing& listing) {
    listing.attempted = true;

    const std::uint64_t group_descriptor_offset = partition_start
        + static_cast<std::uint64_t>(geometry.block_size == 1024 ? 2 : 1) * geometry.block_size;
    const ReadResult descriptor = context.source.read(group_descriptor_offset, 32);
    if (!descriptor.ok()) {
        listing.limitation = "could not read the group descriptor";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }

    const std::uint32_t inode_table_block = le32(descriptor.bytes.data() + 8);
    if (inode_table_block == 0 || geometry.inode_size < 128
        || geometry.inode_size > geometry.block_size) {
        listing.limitation = "unsupported inode-table layout";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }

    const InodeInfo root = read_inode(context, partition_start, inode_table_block, geometry,
                                      root_inode_number);
    if (!root.read) {
        listing.limitation = "could not read the root inode";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }
    if (root.kind != InodeKind::directory) {
        listing.limitation = "the root inode is not a directory";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }
    if (root.mapping == DataMapping::extent_tree) {
        listing.limitation = "extent trees are not supported yet";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }
    if (!root.first_data_byte) {
        listing.limitation = "no direct data block is present";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }

    const ReadResult block = context.source.read(*root.first_data_byte, geometry.block_size);
    if (!block.ok()) {
        listing.limitation = "could not read the directory block";
        context.trace("  ext root directory: " + listing.limitation);
        return;
    }

    context.trace("  ext root directory (first direct block):");
    const std::vector<std::uint8_t>& data = block.bytes;
    std::size_t offset = 0;
    while (offset + 8 <= data.size() && listing.entries.size() < max_directory_entries) {
        const std::uint8_t* entry = data.data() + offset;
        const std::uint32_t inode_number = le32(entry);
        const std::uint16_t record_length = le16(entry + 4);
        const std::size_t name_length = entry[6];
        if (record_length < 8 || record_length % 4 != 0 || record_length > data.size() - offset
            || name_length > static_cast<std::size_t>(record_length) - 8) {
            listing.limitation = "invalid directory-entry record; stopped safely";
            context.trace("    Warning: " + listing.limitation);
            return;
        }
        if (inode_number != 0) {
            DirectoryEntryInfo item;
            item.inode_number = inode_number;
            item.name = read_ascii_field(entry + 8, name_length);
            context.trace("    " + item.name + " (inode " + std::to_string(inode_number) + ")");
            if (item.name != "." && item.name != "..") {
                item.inode = read_inode(context, partition_start, inode_table_block, geometry,
                                        inode_number);
            }
            listing.entries.push_back(std::move(item));
        }
        offset += record_length;
    }
}

} // namespace

void read_ext_metadata(ScanContext& context,
                       std::uint64_t partition_start,
                       FileSystemInfo& file_system) {
    constexpr std::uint64_t superblock_offset = 1024;
    const ReadResult raw = context.source.read(partition_start + superblock_offset, 1024);
    if (!raw.ok()) {
        file_system.limitations.push_back("could not read the complete ext superblock");
        context.trace("  ext metadata: could not read complete superblock");
        return;
    }
    const std::uint8_t* superblock = raw.bytes.data();

    const std::uint32_t log_block_size = le32(superblock + 24);
    if (log_block_size > 6) {
        file_system.limitations.push_back("invalid ext block-size exponent");
        context.trace("  ext metadata: invalid block-size exponent");
        return;
    }

    const std::uint16_t state = le16(superblock + 58);
    const std::uint16_t inode_size = le16(superblock + 88);
    const std::uint32_t incompatible_features = le32(superblock + 96);
    const std::uint32_t inodes_per_group = le32(superblock + 40);

    file_system.volume_label = read_ascii_field(superblock + 120, 16);
    file_system.uuid = format_uuid(superblock + 104);
    file_system.block_size_bytes = 1024U << log_block_size;
    file_system.inode_size_bytes = inode_size;
    file_system.state = state == 1 ? "cleanly unmounted" : state == 2 ? "errors recorded" : "unknown";

    std::string features;
    const auto add_feature = [&features](const char* name) {
        if (!features.empty()) features += ", ";
        features += name;
    };
    if ((incompatible_features & 0x40U) != 0) add_feature("extents");
    if ((incompatible_features & 0x80U) != 0) add_feature("64-bit");
    file_system.features = features;

    {
        std::ostringstream line;
        line << "  ext metadata: label=" << file_system.volume_label
             << " uuid=" << file_system.uuid
             << " block=" << file_system.block_size_bytes
             << " inode=" << inode_size
             << " state=" << file_system.state
             << " incompatible-mask=0x" << std::hex << incompatible_features;
        context.trace(line.str());
    }

    read_root_directory(context, partition_start,
                        {file_system.block_size_bytes, inode_size, inodes_per_group},
                        file_system.root_directory);
}

} // namespace filerelic
