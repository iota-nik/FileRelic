#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "image_inspector.h"

namespace {

std::uint32_t read_little_endian_u32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t read_little_endian_u64(const unsigned char* bytes) {
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t crc32_ieee(const unsigned char* bytes, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            const bool low_bit_set = (crc & 1U) != 0;
            crc >>= 1U;
            if (low_bit_set) {
                crc ^= 0xedb88320U;
            }
        }
    }
    return ~crc;
}

constexpr std::size_t sector_size = 512;
constexpr std::size_t partition_entry_offset = 446;
constexpr std::size_t partition_entry_size = 16;
constexpr std::size_t primary_partition_count = 4;

struct MbrEntry {
    unsigned char type;
    std::uint32_t start_sector;
    std::uint32_t sector_count;
};

MbrEntry read_partition_entry(const std::array<unsigned char, sector_size>& sector,
                              std::size_t index) {
    const std::size_t offset = partition_entry_offset + index * partition_entry_size;
    return {
        sector[offset + 4],
        read_little_endian_u32(sector.data() + offset + 8),
        read_little_endian_u32(sector.data() + offset + 12),
    };
}

bool is_extended_partition(unsigned char type) {
    return type == 0x05 || type == 0x0f || type == 0x85;
}

std::string format_mbr_type_code(unsigned char type) {
    std::ostringstream formatted;
    formatted << "0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned int>(type);
    return formatted.str();
}

bool is_valid_range(std::uintmax_t file_size,
                    std::uintmax_t start_byte,
                    std::uintmax_t length_bytes) {
    return start_byte <= file_size && length_bytes <= file_size - start_byte;
}

bool read_sector(std::ifstream& image,
                 std::uintmax_t file_size,
                 std::uint64_t sector_number,
                 std::array<unsigned char, sector_size>& destination) {
    const std::uintmax_t start_byte = static_cast<std::uintmax_t>(sector_number) * sector_size;
    if (!is_valid_range(file_size, start_byte, destination.size())) {
        return false;
    }

    image.clear();
    image.seekg(static_cast<std::streamoff>(start_byte));
    image.read(reinterpret_cast<char*>(destination.data()), destination.size());
    return image.gcount() == static_cast<std::streamsize>(destination.size());
}

bool matches_bytes_at(std::ifstream& image,
                      std::uintmax_t file_size,
                      std::uintmax_t offset,
                      std::string_view expected) {
    if (offset > file_size || expected.size() > file_size - offset) {
        return false;
    }

    std::vector<char> actual(expected.size());
    image.clear();
    image.seekg(static_cast<std::streamoff>(offset));
    image.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    return image.gcount() == static_cast<std::streamsize>(actual.size())
        && std::equal(actual.begin(), actual.end(), expected.begin());
}

filerelic::FileSystemInfo detect_file_system(std::ifstream& image,
                                             std::uintmax_t file_size,
                                             std::uintmax_t partition_start) {
    if (matches_bytes_at(image, file_size, partition_start + 3, "NTFS    ")) {
        return {filerelic::FileSystemKind::ntfs,
                "probable NTFS (boot-sector signature)", true};
    }
    if (matches_bytes_at(image, file_size, partition_start + 32, "NXSB")) {
        return {filerelic::FileSystemKind::apfs,
                "probable APFS container (NXSB signature)", true};
    }
    if (matches_bytes_at(image, file_size, partition_start, "XFSB")) {
        return {filerelic::FileSystemKind::xfs,
                "probable XFS (superblock signature)", true};
    }
    if (matches_bytes_at(image, file_size, partition_start + 1024 + 56,
                         std::string_view("\x53\xef", 2))) {
        return {filerelic::FileSystemKind::ext_family,
                "probable ext-family filesystem (ext2/ext3/ext4 magic)", true};
    }
    if (matches_bytes_at(image, file_size, partition_start + 0x10040, "_BHRfS_M")) {
        return {filerelic::FileSystemKind::btrfs,
                "probable Btrfs (superblock signature)", true};
    }
    return {};
}

bool read_bytes_at(std::ifstream& image,
                   std::uintmax_t file_size,
                   std::uintmax_t offset,
                   unsigned char* destination,
                   std::size_t length) {
    if (offset > file_size || length > file_size - offset) {
        return false;
    }

    image.clear();
    image.seekg(static_cast<std::streamoff>(offset));
    image.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(length));
    return image.gcount() == static_cast<std::streamsize>(length);
}

std::string read_ascii_field(const unsigned char* bytes, std::size_t length) {
    std::string value;
    for (std::size_t index = 0; index < length && bytes[index] != 0; ++index) {
        const unsigned char character = bytes[index];
        value.push_back(character >= 32 && character <= 126
                            ? static_cast<char>(character)
                            : '?');
    }
    return value.empty() ? "(none)" : value;
}

std::string format_uuid(const unsigned char* uuid) {
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

struct ExtSuperblockInfo {
    std::uint32_t block_size;
    std::uint16_t inode_size;
    std::uint32_t inodes_per_group;
};

void inspect_ext_file_inode(std::ostream& output,
                            std::ifstream& image,
                            std::uintmax_t file_size,
                            std::uintmax_t partition_start,
                            std::uint32_t inode_table_block,
                            const ExtSuperblockInfo& superblock_info,
                            std::uint32_t inode_number,
                            std::string_view name) {
    // This first adapter handles only the first inode group. Full ext4 support
    // will select the appropriate group descriptor for larger inode numbers.
    if (inode_number == 0 || inode_number > superblock_info.inodes_per_group) {
        output << "        inode metadata: outside the first inode group\n";
        return;
    }
    const std::uintmax_t inode_offset = partition_start
        + static_cast<std::uintmax_t>(inode_table_block) * superblock_info.block_size
        + static_cast<std::uintmax_t>(inode_number - 1) * superblock_info.inode_size;
    std::vector<unsigned char> inode(superblock_info.inode_size);
    if (!read_bytes_at(image, file_size, inode_offset, inode.data(), inode.size())) {
        output << "        inode metadata: could not read inode " << inode_number << '\n';
        return;
    }

    const std::uint16_t mode = static_cast<std::uint16_t>(
        inode[0] | (static_cast<std::uint16_t>(inode[1]) << 8U));
    const std::uint32_t size = read_little_endian_u32(inode.data() + 4);
    const std::uint32_t flags = read_little_endian_u32(inode.data() + 32);
    const std::uint16_t type = mode & 0xf000U;
    output << "        inode metadata for " << name << ": ";
    if (type == 0x8000U) {
        output << "regular file\n";
    } else if (type == 0x4000U) {
        output << "directory\n";
    } else if (type == 0xa000U) {
        output << "symbolic link\n";
    } else {
        output << "unsupported type\n";
        return;
    }
    output << "          Size: " << size << " bytes\n";
    if ((flags & 0x00080000U) != 0) {
        output << "          Data mapping: extent tree (not decoded yet)\n";
        return;
    }
    const std::uint32_t first_data_block = read_little_endian_u32(inode.data() + 40);
    if (first_data_block == 0) {
        output << "          Data mapping: no first direct block\n";
        return;
    }
    output << "          First data block: " << first_data_block
              << " (byte " << partition_start
              + static_cast<std::uintmax_t>(first_data_block) * superblock_info.block_size
              << ")\n";
}

void inspect_ext_root_directory(std::ostream& output,
                                std::ifstream& image,
                                std::uintmax_t file_size,
                                std::uintmax_t partition_start,
                                const ExtSuperblockInfo& superblock_info) {
    constexpr std::uint32_t root_inode_number = 2;
    const std::uintmax_t group_descriptor_offset = partition_start
        + (superblock_info.block_size == 1024 ? 2 : 1) * superblock_info.block_size;
    std::array<unsigned char, 32> group_descriptor{};
    if (!read_bytes_at(image, file_size, group_descriptor_offset,
                       group_descriptor.data(), group_descriptor.size())) {
        output << "    ext root directory: could not read group descriptor\n";
        return;
    }

    const std::uint32_t inode_table_block = read_little_endian_u32(group_descriptor.data() + 8);
    if (inode_table_block == 0 || superblock_info.inode_size < 128
        || superblock_info.inode_size > superblock_info.block_size) {
        output << "    ext root directory: unsupported inode-table layout\n";
        return;
    }

    const std::uintmax_t root_inode_offset = partition_start
        + static_cast<std::uintmax_t>(inode_table_block) * superblock_info.block_size
        + static_cast<std::uintmax_t>(root_inode_number - 1) * superblock_info.inode_size;
    std::vector<unsigned char> root_inode(superblock_info.inode_size);
    if (!read_bytes_at(image, file_size, root_inode_offset,
                       root_inode.data(), root_inode.size())) {
        output << "    ext root directory: could not read root inode\n";
        return;
    }

    const std::uint16_t mode = static_cast<std::uint16_t>(
        root_inode[0] | (static_cast<std::uint16_t>(root_inode[1]) << 8U));
    const std::uint32_t inode_flags = read_little_endian_u32(root_inode.data() + 32);
    if ((mode & 0xf000U) != 0x4000U) {
        output << "    ext root directory: root inode is not a directory\n";
        return;
    }
    if ((inode_flags & 0x00080000U) != 0) {
        output << "    ext root directory: extent trees are not supported yet\n";
        return;
    }

    const std::uint32_t first_data_block = read_little_endian_u32(root_inode.data() + 40);
    if (first_data_block == 0) {
        output << "    ext root directory: no direct data block is present\n";
        return;
    }
    const std::uintmax_t directory_offset = partition_start
        + static_cast<std::uintmax_t>(first_data_block) * superblock_info.block_size;
    std::vector<unsigned char> directory_block(superblock_info.block_size);
    if (!read_bytes_at(image, file_size, directory_offset,
                       directory_block.data(), directory_block.size())) {
        output << "    ext root directory: could not read directory block\n";
        return;
    }

    output << "    ext root directory (first direct block):\n";
    std::size_t offset = 0;
    std::size_t entry_count = 0;
    while (offset + 8 <= directory_block.size() && entry_count < 64) {
        const unsigned char* entry = directory_block.data() + offset;
        const std::uint32_t inode_number = read_little_endian_u32(entry);
        const std::uint16_t record_length = static_cast<std::uint16_t>(
            entry[4] | (static_cast<std::uint16_t>(entry[5]) << 8U));
        const std::size_t name_length = entry[6];
        if (record_length < 8 || record_length % 4 != 0
            || record_length > directory_block.size() - offset
            || name_length > record_length - 8) {
            output << "      Warning: invalid directory-entry record; stopped safely\n";
            return;
        }
        if (inode_number != 0) {
            output << "      " << read_ascii_field(entry + 8, name_length)
                      << " (inode " << inode_number << ")\n";
            const std::string name = read_ascii_field(entry + 8, name_length);
            if (name != "." && name != "..") {
                inspect_ext_file_inode(output, image, file_size, partition_start, inode_table_block,
                                       superblock_info, inode_number, name);
            }
            ++entry_count;
        }
        offset += record_length;
    }
}

void inspect_ext_superblock(std::ostream& output,
                            std::ifstream& image,
                            std::uintmax_t file_size,
                            std::uintmax_t partition_start,
                            filerelic::FileSystemInfo& file_system) {
    constexpr std::size_t superblock_offset = 1024;
    std::array<unsigned char, 1024> superblock{};
    if (!read_bytes_at(image, file_size, partition_start + superblock_offset,
                       superblock.data(), superblock.size())) {
        output << "    ext metadata: could not read complete superblock\n";
        return;
    }

    const std::uint32_t log_block_size = read_little_endian_u32(superblock.data() + 24);
    if (log_block_size > 6) {
        output << "    ext metadata: invalid block-size exponent\n";
        return;
    }

    const std::uint16_t filesystem_state = static_cast<std::uint16_t>(
        superblock[58] | (static_cast<std::uint16_t>(superblock[59]) << 8U));
    const std::uint16_t inode_size = static_cast<std::uint16_t>(
        superblock[88] | (static_cast<std::uint16_t>(superblock[89]) << 8U));
    const std::uint32_t incompatible_features =
        read_little_endian_u32(superblock.data() + 96);
    const std::uint32_t inodes_per_group = read_little_endian_u32(superblock.data() + 40);
    file_system.volume_label = read_ascii_field(superblock.data() + 120, 16);
    file_system.uuid = format_uuid(superblock.data() + 104);
    file_system.block_size_bytes = 1024U << log_block_size;
    file_system.inode_size_bytes = inode_size;
    file_system.state = filesystem_state == 1 ? "cleanly unmounted"
        : filesystem_state == 2 ? "errors recorded" : "unknown";
    file_system.features = std::string(
        (incompatible_features & 0x40U) != 0 ? "extents " : "")
        + ((incompatible_features & 0x80U) != 0 ? "64-bit " : "");

    output << "    ext metadata:\n"
              << "      Volume label: " << read_ascii_field(superblock.data() + 120, 16) << '\n'
              << "      UUID: " << format_uuid(superblock.data() + 104) << '\n'
              << "      Block size: " << (1024U << log_block_size) << " bytes\n"
              << "      Inode size: " << inode_size << " bytes\n"
              << "      State: ";
    if (filesystem_state == 1) {
        output << "cleanly unmounted\n";
    } else if (filesystem_state == 2) {
        output << "errors recorded\n";
    } else {
        output << "unknown (0x" << std::hex << filesystem_state << std::dec << ")\n";
    }
    output << "      Features: "
              << ((incompatible_features & 0x40U) != 0 ? "extents " : "")
              << ((incompatible_features & 0x80U) != 0 ? "64-bit " : "")
              << "(incompatible mask 0x" << std::hex << incompatible_features << std::dec << ")\n";
    inspect_ext_root_directory(output, image, file_size, partition_start,
                               {1024U << log_block_size, inode_size, inodes_per_group});
}

filerelic::FileSystemInfo inspect_file_system(std::ostream& output,
                                              std::ifstream& image,
                                              std::uintmax_t file_size,
                                              std::uintmax_t partition_start) {
    filerelic::FileSystemInfo file_system = detect_file_system(image, file_size, partition_start);
    if (file_system.signature_found) {
        output << file_system.label << '\n';
    } else {
        output << "no supported signature found\n";
        file_system.label = "no supported signature found";
    }
    if (file_system.kind == filerelic::FileSystemKind::ext_family) {
        inspect_ext_superblock(output, image, file_size, partition_start, file_system);
    }
    return file_system;
}

struct GptHeader {
    std::uint32_t header_size;
    std::uint64_t current_lba;
    std::uint64_t backup_lba;
    std::uint64_t first_usable_lba;
    std::uint64_t last_usable_lba;
    std::uint64_t partition_entries_lba;
    std::uint32_t partition_entry_count;
    std::uint32_t partition_entry_size;
    std::uint32_t partition_entries_crc32;
};

bool read_gpt_header(std::ifstream& image,
                     std::uintmax_t file_size,
                     std::uint64_t sector_number,
                     GptHeader& header,
                     std::string& error) {
    std::array<unsigned char, sector_size> sector{};
    if (!read_sector(image, file_size, sector_number, sector)) {
        error = "could not read header sector";
        return false;
    }
    if (!std::equal(std::begin(sector), std::begin(sector) + 8, "EFI PART")) {
        error = "EFI PART signature not found";
        return false;
    }

    const std::uint32_t header_size = read_little_endian_u32(sector.data() + 12);
    if (header_size < 92 || header_size > sector.size()) {
        error = "invalid header size";
        return false;
    }

    const std::uint32_t stored_crc32 = read_little_endian_u32(sector.data() + 16);
    std::array<unsigned char, sector_size> header_for_crc = sector;
    std::fill(header_for_crc.begin() + 16, header_for_crc.begin() + 20, 0);
    if (crc32_ieee(header_for_crc.data(), header_size) != stored_crc32) {
        error = "header CRC32 does not match";
        return false;
    }

    header = {
        header_size,
        read_little_endian_u64(sector.data() + 24),
        read_little_endian_u64(sector.data() + 32),
        read_little_endian_u64(sector.data() + 40),
        read_little_endian_u64(sector.data() + 48),
        read_little_endian_u64(sector.data() + 72),
        read_little_endian_u32(sector.data() + 80),
        read_little_endian_u32(sector.data() + 84),
        read_little_endian_u32(sector.data() + 88),
    };
    return true;
}

std::string read_gpt_name(const unsigned char* entry, std::size_t entry_size) {
    std::string name;
    for (std::size_t offset = 56; offset + 1 < entry_size && offset < 128; offset += 2) {
        const unsigned char low_byte = entry[offset];
        const unsigned char high_byte = entry[offset + 1];
        if (low_byte == 0 && high_byte == 0) {
            break;
        }
        name.push_back(high_byte == 0 && low_byte >= 32 && low_byte <= 126
                           ? static_cast<char>(low_byte)
                           : '?');
    }
    return name.empty() ? "unnamed" : name;
}

std::string format_gpt_type_guid(const unsigned char* guid) {
    constexpr std::array<std::size_t, 16> order{3, 2, 1, 0, 5, 4, 7, 6,
                                                 8, 9, 10, 11, 12, 13, 14, 15};
    std::ostringstream formatted;
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            formatted << '-';
        }
        formatted << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(guid[order[index]]);
    }
    return formatted.str();
}

void inspect_gpt(std::ostream& output,
                 std::ifstream& image,
                 std::uintmax_t file_size,
                 filerelic::InspectionReport& report) {
    constexpr std::size_t maximum_partition_entries = 128;
    const std::uintmax_t total_sectors = file_size / sector_size;
    report.partition_table = filerelic::PartitionTableKind::gpt;
    GptHeader primary{};
    std::string error;
    if (!read_gpt_header(image, file_size, 1, primary, error)) {
        output << "  GPT: primary header invalid; " << error << '\n';
        report.partition_table_state = filerelic::ValidationState::invalid;
        report.warnings.push_back("GPT primary header invalid: " + error);
        return;
    }

    report.partition_table_state = filerelic::ValidationState::valid;
    output << "  GPT: primary header valid (CRC32 checked)\n";
    bool has_warning = false;
    if (primary.current_lba != 1 || primary.backup_lba >= total_sectors
        || primary.first_usable_lba > primary.last_usable_lba) {
        output << "  GPT warning: inconsistent primary-header locations or usable range\n";
        report.warnings.push_back("GPT primary header has inconsistent locations or usable range");
        has_warning = true;
    }

    GptHeader backup{};
    if (!read_gpt_header(image, file_size, primary.backup_lba, backup, error)) {
        output << "  GPT warning: backup header invalid; " << error << '\n';
        report.warnings.push_back("GPT backup header invalid: " + error);
        has_warning = true;
    } else if (backup.current_lba != primary.backup_lba || backup.backup_lba != 1) {
        output << "  GPT warning: backup header does not point back to primary header\n";
        report.warnings.push_back("GPT backup header does not point back to primary header");
        has_warning = true;
    } else {
        output << "  GPT: backup header valid (CRC32 checked)\n";
    }

    if (primary.partition_entry_count == 0 || primary.partition_entry_count > maximum_partition_entries
        || primary.partition_entry_size < 128 || primary.partition_entry_size > 4096) {
        output << "  GPT warning: unsupported partition-entry count or size\n";
        report.warnings.push_back("GPT partition-entry count or size is unsupported");
        report.partition_table_state = filerelic::ValidationState::warning;
        return;
    }

    const std::uintmax_t entries_length =
        static_cast<std::uintmax_t>(primary.partition_entry_count) * primary.partition_entry_size;
    const std::uintmax_t entries_start = primary.partition_entries_lba * sector_size;
    if (!is_valid_range(file_size, entries_start, entries_length)) {
        output << "  GPT warning: partition-entry array extends beyond image boundary\n";
        report.warnings.push_back("GPT partition-entry array extends beyond image boundary");
        report.partition_table_state = filerelic::ValidationState::invalid;
        return;
    }

    std::vector<unsigned char> entries(entries_length);
    image.clear();
    image.seekg(static_cast<std::streamoff>(entries_start));
    image.read(reinterpret_cast<char*>(entries.data()),
               static_cast<std::streamsize>(entries.size()));
    if (image.gcount() != static_cast<std::streamsize>(entries.size())) {
        output << "  GPT warning: could not read complete partition-entry array\n";
        report.warnings.push_back("Could not read complete GPT partition-entry array");
        report.partition_table_state = filerelic::ValidationState::invalid;
        return;
    }
    if (crc32_ieee(entries.data(), entries.size()) != primary.partition_entries_crc32) {
        output << "  GPT warning: partition-entry array CRC32 does not match\n";
        report.warnings.push_back("GPT partition-entry array CRC32 does not match");
        report.partition_table_state = filerelic::ValidationState::invalid;
        return;
    }
    output << "  GPT: partition-entry array valid (CRC32 checked)\n";

    struct Range {
        std::uint64_t start_lba;
        std::uint64_t end_lba;
    };
    std::vector<Range> partition_ranges;

    for (std::size_t index = 0; index < primary.partition_entry_count; ++index) {
        const unsigned char* entry = entries.data() + index * primary.partition_entry_size;
        const bool is_unused = std::all_of(entry, entry + 16,
                                           [](unsigned char byte) { return byte == 0; });
        if (is_unused) {
            continue;
        }

        const std::uint64_t start_lba = read_little_endian_u64(entry + 32);
        const std::uint64_t end_lba = read_little_endian_u64(entry + 40);
        const bool range_is_valid = start_lba <= end_lba
            && start_lba >= primary.first_usable_lba
            && end_lba <= primary.last_usable_lba
            && end_lba < total_sectors;
        filerelic::PartitionInfo partition;
        partition.table = filerelic::PartitionTableKind::gpt;
        partition.number = index + 1;
        partition.name = read_gpt_name(entry, primary.partition_entry_size);
        partition.type_id = format_gpt_type_guid(entry);
        partition.start_sector = start_lba;
        const std::uint64_t lba_difference = end_lba >= start_lba ? end_lba - start_lba : 0;
        partition.sector_count = end_lba >= start_lba
                && lba_difference < std::numeric_limits<std::uint64_t>::max()
            ? lba_difference + 1 : 0;
        partition.range_valid = range_is_valid;
        if (range_is_valid) {
            partition.start_byte = start_lba * sector_size;
            partition.length_bytes = partition.sector_count * sector_size;
        }

        output << "  GPT partition " << index + 1 << " (" 
                  << partition.name << "):\n"
                  << "    Starts: sector " << start_lba << '\n'
                  << "    Ends: sector " << end_lba << '\n';
        if (!range_is_valid) {
            output << "    Status: invalid partition range\n";
            partition.warnings.push_back("Partition range is invalid");
            report.warnings.push_back("GPT partition " + std::to_string(index + 1)
                                      + " has an invalid range");
            report.partitions.push_back(std::move(partition));
            has_warning = true;
            continue;
        }

        const std::uintmax_t start_byte = start_lba * sector_size;
        output << "    Length: " << end_lba - start_lba + 1 << " sectors\n"
                  << "    File system: ";
        partition.file_system = inspect_file_system(output, image, file_size, start_byte);
        report.partitions.push_back(std::move(partition));
        partition_ranges.push_back({start_lba, end_lba});
    }

    for (std::size_t left = 0; left < partition_ranges.size(); ++left) {
        for (std::size_t right = left + 1; right < partition_ranges.size(); ++right) {
            const auto& first = partition_ranges[left];
            const auto& second = partition_ranges[right];
            if (first.start_lba <= second.end_lba && second.start_lba <= first.end_lba) {
                output << "  GPT warning: valid partition ranges overlap\n";
                report.warnings.push_back("Valid GPT partition ranges overlap");
                has_warning = true;
            }
        }
    }

    output << "  GPT: " << (has_warning ? "layout has warnings" : "likely valid layout")
              << '\n';
    if (has_warning && report.partition_table_state == filerelic::ValidationState::valid) {
        report.partition_table_state = filerelic::ValidationState::warning;
    }
}

void inspect_extended_partition(std::ostream& output,
                                std::ifstream& image,
                                std::uintmax_t file_size,
                                std::uint32_t extended_start_sector,
                                std::uint32_t extended_sector_count,
                                filerelic::InspectionReport& report) {
    const std::uintmax_t extended_start_byte =
        static_cast<std::uintmax_t>(extended_start_sector) * sector_size;
    const std::uintmax_t extended_length_bytes =
        static_cast<std::uintmax_t>(extended_sector_count) * sector_size;
    const std::uintmax_t extended_end_byte = extended_start_byte + extended_length_bytes;
    const auto record_warning = [&report](std::string message) {
        report.warnings.push_back(std::move(message));
        report.partition_table_state = filerelic::ValidationState::warning;
    };

    output << "  Status: extended partition; reading EBR chain\n";
    std::vector<std::uint64_t> visited_ebr_sectors;
    std::uint64_t current_ebr_sector = extended_start_sector;
    constexpr std::size_t maximum_ebr_records = 128;

    for (std::size_t record = 0; record < maximum_ebr_records; ++record) {
        if (std::find(visited_ebr_sectors.begin(), visited_ebr_sectors.end(),
                      current_ebr_sector) != visited_ebr_sectors.end()) {
            output << "  Warning: EBR chain contains a loop at sector "
                      << current_ebr_sector << '\n';
            record_warning("EBR chain contains a loop at sector "
                           + std::to_string(current_ebr_sector));
            return;
        }
        visited_ebr_sectors.push_back(current_ebr_sector);

        const std::uintmax_t ebr_start_byte = current_ebr_sector * sector_size;
        if (!is_valid_range(file_size, ebr_start_byte, sector_size)
            || ebr_start_byte < extended_start_byte
            || ebr_start_byte + sector_size > extended_end_byte) {
            output << "  Warning: EBR at sector " << current_ebr_sector
                      << " lies outside the extended partition\n";
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " lies outside the extended partition");
            return;
        }

        std::array<unsigned char, sector_size> ebr{};
        if (!read_sector(image, file_size, current_ebr_sector, ebr)) {
            output << "  Warning: could not read EBR at sector "
                      << current_ebr_sector << '\n';
            record_warning("Could not read EBR at sector "
                           + std::to_string(current_ebr_sector));
            return;
        }
        if (ebr[510] != 0x55 || ebr[511] != 0xaa) {
            output << "  Warning: EBR at sector " << current_ebr_sector
                      << " has no 55 aa signature\n";
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " has no 55 aa signature");
            return;
        }

        const MbrEntry logical_entry = read_partition_entry(ebr, 0);
        const MbrEntry next_ebr_entry = read_partition_entry(ebr, 1);

        if (logical_entry.type == 0 || logical_entry.sector_count == 0) {
            output << "  Warning: EBR at sector " << current_ebr_sector
                      << " has no valid logical-partition entry\n";
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " has no valid logical-partition entry");
            return;
        }

        const std::uint64_t logical_start_sector =
            current_ebr_sector + logical_entry.start_sector;
        const std::uintmax_t logical_start_byte = logical_start_sector * sector_size;
        const std::uintmax_t logical_length_bytes =
            static_cast<std::uintmax_t>(logical_entry.sector_count) * sector_size;
        const bool logical_range_is_valid =
            is_valid_range(file_size, logical_start_byte, logical_length_bytes)
            && logical_start_byte >= extended_start_byte
            && logical_start_byte + logical_length_bytes <= extended_end_byte;

        output << "  Logical partition " << record + 1 << ":\n"
                  << "    Type: 0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(logical_entry.type) << std::dec << '\n'
                  << "    Starts: sector " << logical_start_sector
                  << " (byte " << logical_start_byte << ")\n"
                  << "    Length: " << logical_entry.sector_count
                  << " sectors (" << logical_length_bytes << " bytes)\n";

        if (!logical_range_is_valid) {
            output << "    Status: invalid; extends beyond extended-partition boundary\n";
            filerelic::PartitionInfo partition;
            partition.table = filerelic::PartitionTableKind::mbr;
            partition.number = record + 1;
            partition.type_code = logical_entry.type;
            partition.type_id = format_mbr_type_code(logical_entry.type);
            partition.start_sector = logical_start_sector;
            partition.sector_count = logical_entry.sector_count;
            partition.start_byte = logical_start_byte;
            partition.length_bytes = logical_length_bytes;
            partition.range_valid = false;
            partition.warnings.push_back("Logical partition exceeds the extended-partition boundary");
            report.partitions.push_back(std::move(partition));
            report.warnings.push_back("Logical MBR partition " + std::to_string(record + 1)
                                      + " exceeds the extended-partition boundary");
            report.partition_table_state = filerelic::ValidationState::warning;
            return;
        }
        output << "    File system: ";
        filerelic::PartitionInfo partition;
        partition.table = filerelic::PartitionTableKind::mbr;
        partition.number = record + 1;
        partition.type_code = logical_entry.type;
        partition.type_id = format_mbr_type_code(logical_entry.type);
        partition.start_sector = logical_start_sector;
        partition.sector_count = logical_entry.sector_count;
        partition.start_byte = logical_start_byte;
        partition.length_bytes = logical_length_bytes;
        partition.range_valid = true;
        partition.file_system = inspect_file_system(output, image, file_size, logical_start_byte);
        report.partitions.push_back(std::move(partition));

        if (next_ebr_entry.type == 0 && next_ebr_entry.sector_count == 0) {
            output << "  EBR chain complete\n";
            return;
        }
        if (!is_extended_partition(next_ebr_entry.type)
            || next_ebr_entry.sector_count == 0) {
            output << "  Warning: EBR at sector " << current_ebr_sector
                      << " has an invalid next-EBR entry\n";
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " has an invalid next-EBR entry");
            return;
        }

        current_ebr_sector = static_cast<std::uint64_t>(extended_start_sector)
            + next_ebr_entry.start_sector;
    }

    output << "  Warning: EBR chain exceeded " << maximum_ebr_records
              << " records; stopped safely\n";
    record_warning("EBR chain exceeded the safe limit of "
                   + std::to_string(maximum_ebr_records) + " records");
}

void inspect_mbr(std::ostream& output,
                 std::ifstream& image,
                 std::uintmax_t file_size,
                 filerelic::InspectionReport& report) {
    if (file_size < sector_size) {
        output << "MBR: image is smaller than one sector; not present\n";
        report.diagnostics.push_back("Image is smaller than one sector; no MBR can be present");
        return;
    }

    std::array<unsigned char, sector_size> first_sector{};
    image.clear();
    image.seekg(0);
    image.read(reinterpret_cast<char*>(first_sector.data()), first_sector.size());
    if (image.gcount() != static_cast<std::streamsize>(first_sector.size())) {
        output << "MBR: could not read the first sector\n";
        report.diagnostics.push_back("Could not read the image's first sector");
        return;
    }

    if (first_sector[510] != 0x55 || first_sector[511] != 0xaa) {
        output << "MBR: signature not found\n";
        report.diagnostics.push_back("No MBR signature found at byte offsets 510–511");
        return;
    }

    output << "MBR: signature found (55 aa); validating partition entries\n";
    report.partition_table = filerelic::PartitionTableKind::mbr;
    report.partition_table_state = filerelic::ValidationState::valid;

    struct Partition {
        std::size_t number;
        unsigned char type;
        std::uint32_t start_sector;
        std::uint32_t sector_count;
        std::uintmax_t start_byte;
        std::uintmax_t length_bytes;
        bool range_is_valid;
    };

    std::vector<Partition> partitions;
    bool has_invalid_entry = false;

    for (std::size_t index = 0; index < primary_partition_count; ++index) {
        const MbrEntry entry = read_partition_entry(first_sector, index);
        const unsigned char type = entry.type;
        const std::uint32_t start_sector = entry.start_sector;
        const std::uint32_t sector_count = entry.sector_count;

        if (type == 0 && sector_count == 0) {
            continue;
        }
        if (type == 0 || sector_count == 0) {
            output << "Partition " << index + 1
                      << ": malformed entry (type and length must both be non-zero)\n";
            report.warnings.push_back("MBR partition entry " + std::to_string(index + 1)
                                      + " has only one of type or length set");
            has_invalid_entry = true;
            continue;
        }

        const std::uintmax_t start_byte =
            static_cast<std::uintmax_t>(start_sector) * sector_size;
        const std::uintmax_t length_bytes =
            static_cast<std::uintmax_t>(sector_count) * sector_size;
        const bool range_is_valid = start_byte <= file_size
            && length_bytes <= file_size - start_byte;

        partitions.push_back({index + 1, type, start_sector, sector_count,
                              start_byte, length_bytes, range_is_valid});
    }

    bool has_overlap = false;
    for (std::size_t left = 0; left < partitions.size(); ++left) {
        for (std::size_t right = left + 1; right < partitions.size(); ++right) {
            const auto& first = partitions[left];
            const auto& second = partitions[right];
            if (!first.range_is_valid || !second.range_is_valid) {
                continue;
            }
            const std::uintmax_t first_end = first.start_byte + first.length_bytes;
            const std::uintmax_t second_end = second.start_byte + second.length_bytes;
            if (first.start_byte < second_end && second.start_byte < first_end) {
                output << "Warning: partitions " << first.number << " and "
                          << second.number << " overlap\n";
                report.warnings.push_back("MBR partitions " + std::to_string(first.number)
                                          + " and " + std::to_string(second.number)
                                          + " overlap");
                has_overlap = true;
            }
        }
    }

    if (partitions.empty()) {
        output << "MBR: signature found, but no populated primary entries\n";
        report.partition_table_state = filerelic::ValidationState::warning;
        report.warnings.push_back("MBR signature exists, but no primary partition entries are populated");
        return;
    }

    for (const auto& partition : partitions) {
        output << "Partition " << partition.number << ":\n"
                  << "  Type: 0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(partition.type) << std::dec << '\n'
                  << "  Starts: sector " << partition.start_sector
                  << " (byte " << partition.start_byte << ")\n"
                  << "  Length: " << partition.sector_count
                  << " sectors (" << partition.length_bytes << " bytes)\n";

        if (!partition.range_is_valid) {
            output << "  Status: invalid; extends beyond image boundary\n";
            filerelic::PartitionInfo record;
            record.table = filerelic::PartitionTableKind::mbr;
            record.number = partition.number;
            record.type_code = partition.type;
            record.type_id = format_mbr_type_code(partition.type);
            record.start_sector = partition.start_sector;
            record.sector_count = partition.sector_count;
            record.start_byte = partition.start_byte;
            record.length_bytes = partition.length_bytes;
            record.range_valid = false;
            record.warnings.push_back("Partition extends beyond image boundary");
            report.partitions.push_back(std::move(record));
            report.warnings.push_back("MBR partition " + std::to_string(partition.number)
                                      + " extends beyond image boundary");
            has_invalid_entry = true;
            continue;
        }

        if (is_extended_partition(partition.type)) {
            filerelic::PartitionInfo record;
            record.table = filerelic::PartitionTableKind::mbr;
            record.number = partition.number;
            record.type_code = partition.type;
            record.type_id = format_mbr_type_code(partition.type);
            record.start_sector = partition.start_sector;
            record.sector_count = partition.sector_count;
            record.start_byte = partition.start_byte;
            record.length_bytes = partition.length_bytes;
            record.range_valid = true;
            record.extended = true;
            report.partitions.push_back(std::move(record));
            inspect_extended_partition(output, image, file_size, partition.start_sector,
                                       partition.sector_count, report);
        } else if (partition.type == 0xee) {
            output << "  Status: GPT protective partition\n";
            inspect_gpt(output, image, file_size, report);
        } else {
            output << "  File system: ";
            filerelic::PartitionInfo record;
            record.table = filerelic::PartitionTableKind::mbr;
            record.number = partition.number;
            record.type_code = partition.type;
            record.type_id = format_mbr_type_code(partition.type);
            record.start_sector = partition.start_sector;
            record.sector_count = partition.sector_count;
            record.start_byte = partition.start_byte;
            record.length_bytes = partition.length_bytes;
            record.range_valid = true;
            record.file_system = inspect_file_system(output, image, file_size, partition.start_byte);
            report.partitions.push_back(std::move(record));
        }
    }

    if (!has_invalid_entry && !has_overlap) {
        output << "MBR: likely partition table; all populated primary entries fit the image\n";
    } else {
        output << "MBR: signature found, but entry validation raised warnings\n";
        report.partition_table_state = filerelic::ValidationState::warning;
    }
}

int inspect_image_file(const std::filesystem::path& image_path,
                       std::ostream& output,
                       filerelic::InspectionReport& report) {
    std::error_code error;
    const bool is_regular_file = std::filesystem::is_regular_file(image_path, error);
    if (error || !is_regular_file) {
        output << "Error: path is not a readable regular file: "
                  << image_path.string() << '\n';
        report.diagnostics.push_back("Path is not a readable regular file");
        return 1;
    }

    const std::uintmax_t file_size = std::filesystem::file_size(image_path, error);
    if (error) {
        output << "Error: could not determine file size: " << error.message() << '\n';
        report.diagnostics.push_back("Could not determine image file size: " + error.message());
        return 1;
    }

    std::ifstream image(image_path, std::ios::binary);
    if (!image) {
        output << "Error: could not open file for reading: "
                  << image_path.string() << '\n';
        report.diagnostics.push_back("Could not open image file for reading");
        return 1;
    }

    constexpr std::size_t preview_limit = 16;
    std::array<unsigned char, preview_limit> preview{};
    const auto preview_size = static_cast<std::streamsize>(
        std::min<std::uintmax_t>(file_size, preview.size()));
    image.read(reinterpret_cast<char*>(preview.data()), preview_size);
    if (image.gcount() != preview_size) {
        output << "Error: file changed or could not be read completely during inspection.\n";
        report.diagnostics.push_back("Image changed or could not be read completely during inspection");
        return 1;
    }

    output << "Image file: " << image_path.string() << '\n'
              << "Size: " << file_size << " bytes\n"
              << "First " << preview_size << " bytes (hex):";

    for (std::streamsize i = 0; i < preview_size; ++i) {
        output << ' ' << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(preview[static_cast<std::size_t>(i)]);
    }
    output << std::dec << '\n';

    inspect_mbr(output, image, file_size, report);
    return 0;
}

} // namespace

filerelic::InspectionReport filerelic::inspect_image(const std::filesystem::path& image_path) {
    InspectionReport report{};
    report.image_path = image_path;

    std::error_code error;
    report.image_size_bytes = std::filesystem::file_size(image_path, error);
    if (error) {
        report.image_size_bytes = 0;
    }

    // Keep a detailed text trace for CLI compatibility and advanced GUI
    // diagnostics. Parser routines also populate typed fields on report itself.
    std::ostringstream captured_output;
    report.exit_code = inspect_image_file(image_path, captured_output, report);
    report.rendered_text = captured_output.str();

    // Build presentation sections from the typed results produced by the
    // inspection routines. This is the shared UI/CLI-facing model; no report
    // text is reparsed to discover partitions or warnings.
    report.sections.push_back({
        filerelic::ReportSectionKind::image,
        "Image",
        image_path.string() + "\n" + std::to_string(report.image_size_bytes) + " bytes",
        false,
    });

    if (report.partition_table != filerelic::PartitionTableKind::unknown) {
        const std::string table_name = report.partition_table == filerelic::PartitionTableKind::gpt
            ? "GPT" : "MBR";
        const std::string state = report.partition_table_state == filerelic::ValidationState::valid
            ? "validated" : report.partition_table_state == filerelic::ValidationState::invalid
                ? "invalid" : report.partition_table_state == filerelic::ValidationState::warning
                    ? "has warnings" : "unknown";
        report.sections.push_back({
            filerelic::ReportSectionKind::partition_layout,
            "Partition layout",
            table_name + " partition table " + state,
            report.partition_table_state == filerelic::ValidationState::warning
                || report.partition_table_state == filerelic::ValidationState::invalid,
        });
    }

    for (const auto& partition : report.partitions) {
        const std::string scheme = partition.table == filerelic::PartitionTableKind::gpt
            ? "GPT" : "MBR";
        std::string title = scheme + " partition " + std::to_string(partition.number);
        if (!partition.name.empty()) {
            title += " (" + partition.name + ")";
        }
        std::string details;
        const auto append_detail = [&details](const std::string& value) {
            if (!details.empty()) details.push_back('\n');
            details += value;
        };
        if (!partition.type_id.empty()) append_detail("Type: " + partition.type_id);
        append_detail("Starts: sector " + std::to_string(partition.start_sector)
                      + " (byte " + std::to_string(partition.start_byte) + ")");
        append_detail("Length: " + std::to_string(partition.sector_count)
                      + " sectors (" + std::to_string(partition.length_bytes) + " bytes)");
        append_detail(std::string("Status: ") + (partition.range_valid ? "within image bounds" : "invalid range"));
        if (partition.extended) {
            append_detail("Contains an extended MBR logical-partition chain");
        }
        if (partition.file_system.signature_found) {
            append_detail("File system: " + partition.file_system.label);
        } else if (!partition.extended && partition.range_valid) {
            append_detail("File system: no supported signature found");
        }
        if (!partition.file_system.volume_label.empty()) {
            append_detail("Volume label: " + partition.file_system.volume_label);
        }
        if (!partition.file_system.uuid.empty()) {
            append_detail("UUID: " + partition.file_system.uuid);
        }
        if (partition.file_system.block_size_bytes != 0) {
            append_detail("Block size: " + std::to_string(partition.file_system.block_size_bytes) + " bytes");
        }
        if (partition.file_system.inode_size_bytes != 0) {
            append_detail("Inode size: " + std::to_string(partition.file_system.inode_size_bytes) + " bytes");
        }
        if (!partition.file_system.state.empty()) append_detail("Filesystem state: " + partition.file_system.state);
        if (!partition.file_system.features.empty()) append_detail("Features: " + partition.file_system.features);
        for (const auto& warning : partition.warnings) append_detail("Warning: " + warning);
        report.sections.push_back({filerelic::ReportSectionKind::partition,
                                   std::move(title), std::move(details),
                                   !partition.warnings.empty() || !partition.range_valid});
    }

    if (!report.warnings.empty()) {
        std::string details;
        for (const auto& warning : report.warnings) {
            if (!details.empty()) details.push_back('\n');
            details += warning;
        }
        report.sections.push_back({filerelic::ReportSectionKind::warnings,
                                   "Warnings", std::move(details), true});
    }
    if (!report.diagnostics.empty()) {
        std::string details;
        for (const auto& diagnostic : report.diagnostics) {
            if (!details.empty()) details.push_back('\n');
            details += diagnostic;
        }
        report.sections.push_back({filerelic::ReportSectionKind::diagnostic,
                                   "Diagnostic", std::move(details), !report.succeeded()});
    }
    return report;
}

void filerelic::render_report(const InspectionReport& report, std::ostream& output) {
    output << report.rendered_text;
}
