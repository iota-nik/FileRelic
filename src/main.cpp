#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "image_inspector.h"

namespace {

void print_help() {
    std::cout << "FileRelic " << FILERELIC_VERSION << "\n"
              << "Forensic recovery toolkit (development scaffold)\n\n"
              << "Usage:\n"
              << "  filerelic --version   Print the application version\n"
              << "  filerelic --help      Show this help\n"
              << "  filerelic inspect <path>\n"
              << "                       Inspect a regular image file read-only\n"
              << "  filerelic inspect-image <path>\n"
              << "                       Compatibility name for inspect\n"
              << "  filerelic demo        Run the bundled safe inspection demonstration\n";
}

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

std::string detect_file_system(std::ifstream& image,
                               std::uintmax_t file_size,
                               std::uintmax_t partition_start) {
    if (matches_bytes_at(image, file_size, partition_start + 3, "NTFS    ")) {
        return "probable NTFS (boot-sector signature)";
    }
    if (matches_bytes_at(image, file_size, partition_start + 32, "NXSB")) {
        return "probable APFS container (NXSB signature)";
    }
    if (matches_bytes_at(image, file_size, partition_start, "XFSB")) {
        return "probable XFS (superblock signature)";
    }
    if (matches_bytes_at(image, file_size, partition_start + 1024 + 56,
                         std::string_view("\x53\xef", 2))) {
        return "probable ext-family filesystem (ext2/ext3/ext4 magic)";
    }
    if (matches_bytes_at(image, file_size, partition_start + 0x10040, "_BHRfS_M")) {
        return "probable Btrfs (superblock signature)";
    }
    return "no supported signature found";
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
};

void inspect_ext_root_directory(std::ifstream& image,
                                std::uintmax_t file_size,
                                std::uintmax_t partition_start,
                                const ExtSuperblockInfo& superblock_info) {
    constexpr std::uint32_t root_inode_number = 2;
    const std::uintmax_t group_descriptor_offset = partition_start
        + (superblock_info.block_size == 1024 ? 2 : 1) * superblock_info.block_size;
    std::array<unsigned char, 32> group_descriptor{};
    if (!read_bytes_at(image, file_size, group_descriptor_offset,
                       group_descriptor.data(), group_descriptor.size())) {
        std::cout << "    ext root directory: could not read group descriptor\n";
        return;
    }

    const std::uint32_t inode_table_block = read_little_endian_u32(group_descriptor.data() + 8);
    if (inode_table_block == 0 || superblock_info.inode_size < 128
        || superblock_info.inode_size > superblock_info.block_size) {
        std::cout << "    ext root directory: unsupported inode-table layout\n";
        return;
    }

    const std::uintmax_t root_inode_offset = partition_start
        + static_cast<std::uintmax_t>(inode_table_block) * superblock_info.block_size
        + static_cast<std::uintmax_t>(root_inode_number - 1) * superblock_info.inode_size;
    std::vector<unsigned char> root_inode(superblock_info.inode_size);
    if (!read_bytes_at(image, file_size, root_inode_offset,
                       root_inode.data(), root_inode.size())) {
        std::cout << "    ext root directory: could not read root inode\n";
        return;
    }

    const std::uint16_t mode = static_cast<std::uint16_t>(
        root_inode[0] | (static_cast<std::uint16_t>(root_inode[1]) << 8U));
    const std::uint32_t inode_flags = read_little_endian_u32(root_inode.data() + 32);
    if ((mode & 0xf000U) != 0x4000U) {
        std::cout << "    ext root directory: root inode is not a directory\n";
        return;
    }
    if ((inode_flags & 0x00080000U) != 0) {
        std::cout << "    ext root directory: extent trees are not supported yet\n";
        return;
    }

    const std::uint32_t first_data_block = read_little_endian_u32(root_inode.data() + 40);
    if (first_data_block == 0) {
        std::cout << "    ext root directory: no direct data block is present\n";
        return;
    }
    const std::uintmax_t directory_offset = partition_start
        + static_cast<std::uintmax_t>(first_data_block) * superblock_info.block_size;
    std::vector<unsigned char> directory_block(superblock_info.block_size);
    if (!read_bytes_at(image, file_size, directory_offset,
                       directory_block.data(), directory_block.size())) {
        std::cout << "    ext root directory: could not read directory block\n";
        return;
    }

    std::cout << "    ext root directory (first direct block):\n";
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
            std::cout << "      Warning: invalid directory-entry record; stopped safely\n";
            return;
        }
        if (inode_number != 0) {
            std::cout << "      " << read_ascii_field(entry + 8, name_length)
                      << " (inode " << inode_number << ")\n";
            ++entry_count;
        }
        offset += record_length;
    }
}

void inspect_ext_superblock(std::ifstream& image,
                            std::uintmax_t file_size,
                            std::uintmax_t partition_start) {
    constexpr std::size_t superblock_offset = 1024;
    std::array<unsigned char, 1024> superblock{};
    if (!read_bytes_at(image, file_size, partition_start + superblock_offset,
                       superblock.data(), superblock.size())) {
        std::cout << "    ext metadata: could not read complete superblock\n";
        return;
    }

    const std::uint32_t log_block_size = read_little_endian_u32(superblock.data() + 24);
    if (log_block_size > 6) {
        std::cout << "    ext metadata: invalid block-size exponent\n";
        return;
    }

    const std::uint16_t filesystem_state = static_cast<std::uint16_t>(
        superblock[58] | (static_cast<std::uint16_t>(superblock[59]) << 8U));
    const std::uint16_t inode_size = static_cast<std::uint16_t>(
        superblock[88] | (static_cast<std::uint16_t>(superblock[89]) << 8U));
    const std::uint32_t incompatible_features =
        read_little_endian_u32(superblock.data() + 96);

    std::cout << "    ext metadata:\n"
              << "      Volume label: " << read_ascii_field(superblock.data() + 120, 16) << '\n'
              << "      UUID: " << format_uuid(superblock.data() + 104) << '\n'
              << "      Block size: " << (1024U << log_block_size) << " bytes\n"
              << "      Inode size: " << inode_size << " bytes\n"
              << "      State: ";
    if (filesystem_state == 1) {
        std::cout << "cleanly unmounted\n";
    } else if (filesystem_state == 2) {
        std::cout << "errors recorded\n";
    } else {
        std::cout << "unknown (0x" << std::hex << filesystem_state << std::dec << ")\n";
    }
    std::cout << "      Features: "
              << ((incompatible_features & 0x40U) != 0 ? "extents " : "")
              << ((incompatible_features & 0x80U) != 0 ? "64-bit " : "")
              << "(incompatible mask 0x" << std::hex << incompatible_features << std::dec << ")\n";
    inspect_ext_root_directory(image, file_size, partition_start,
                               {1024U << log_block_size, inode_size});
}

void print_file_system_report(std::ifstream& image,
                              std::uintmax_t file_size,
                              std::uintmax_t partition_start) {
    const std::string detected = detect_file_system(image, file_size, partition_start);
    std::cout << detected << '\n';
    if (detected.find("ext-family") != std::string::npos) {
        inspect_ext_superblock(image, file_size, partition_start);
    }
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

void inspect_gpt(std::ifstream& image, std::uintmax_t file_size) {
    constexpr std::size_t maximum_partition_entries = 128;
    const std::uintmax_t total_sectors = file_size / sector_size;
    GptHeader primary{};
    std::string error;
    if (!read_gpt_header(image, file_size, 1, primary, error)) {
        std::cout << "  GPT: primary header invalid; " << error << '\n';
        return;
    }

    std::cout << "  GPT: primary header valid (CRC32 checked)\n";
    bool has_warning = false;
    if (primary.current_lba != 1 || primary.backup_lba >= total_sectors
        || primary.first_usable_lba > primary.last_usable_lba) {
        std::cout << "  GPT warning: inconsistent primary-header locations or usable range\n";
        has_warning = true;
    }

    GptHeader backup{};
    if (!read_gpt_header(image, file_size, primary.backup_lba, backup, error)) {
        std::cout << "  GPT warning: backup header invalid; " << error << '\n';
        has_warning = true;
    } else if (backup.current_lba != primary.backup_lba || backup.backup_lba != 1) {
        std::cout << "  GPT warning: backup header does not point back to primary header\n";
        has_warning = true;
    } else {
        std::cout << "  GPT: backup header valid (CRC32 checked)\n";
    }

    if (primary.partition_entry_count == 0 || primary.partition_entry_count > maximum_partition_entries
        || primary.partition_entry_size < 128 || primary.partition_entry_size > 4096) {
        std::cout << "  GPT warning: unsupported partition-entry count or size\n";
        return;
    }

    const std::uintmax_t entries_length =
        static_cast<std::uintmax_t>(primary.partition_entry_count) * primary.partition_entry_size;
    const std::uintmax_t entries_start = primary.partition_entries_lba * sector_size;
    if (!is_valid_range(file_size, entries_start, entries_length)) {
        std::cout << "  GPT warning: partition-entry array extends beyond image boundary\n";
        return;
    }

    std::vector<unsigned char> entries(entries_length);
    image.clear();
    image.seekg(static_cast<std::streamoff>(entries_start));
    image.read(reinterpret_cast<char*>(entries.data()),
               static_cast<std::streamsize>(entries.size()));
    if (image.gcount() != static_cast<std::streamsize>(entries.size())) {
        std::cout << "  GPT warning: could not read complete partition-entry array\n";
        return;
    }
    if (crc32_ieee(entries.data(), entries.size()) != primary.partition_entries_crc32) {
        std::cout << "  GPT warning: partition-entry array CRC32 does not match\n";
        return;
    }
    std::cout << "  GPT: partition-entry array valid (CRC32 checked)\n";

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

        std::cout << "  GPT partition " << index + 1 << " (" 
                  << read_gpt_name(entry, primary.partition_entry_size) << "):\n"
                  << "    Starts: sector " << start_lba << '\n'
                  << "    Ends: sector " << end_lba << '\n';
        if (!range_is_valid) {
            std::cout << "    Status: invalid partition range\n";
            has_warning = true;
            continue;
        }

        const std::uintmax_t start_byte = start_lba * sector_size;
        std::cout << "    Length: " << end_lba - start_lba + 1 << " sectors\n"
                  << "    File system: ";
        print_file_system_report(image, file_size, start_byte);
        partition_ranges.push_back({start_lba, end_lba});
    }

    for (std::size_t left = 0; left < partition_ranges.size(); ++left) {
        for (std::size_t right = left + 1; right < partition_ranges.size(); ++right) {
            const auto& first = partition_ranges[left];
            const auto& second = partition_ranges[right];
            if (first.start_lba <= second.end_lba && second.start_lba <= first.end_lba) {
                std::cout << "  GPT warning: valid partition ranges overlap\n";
                has_warning = true;
            }
        }
    }

    std::cout << "  GPT: " << (has_warning ? "layout has warnings" : "likely valid layout")
              << '\n';
}

void inspect_extended_partition(std::ifstream& image,
                                std::uintmax_t file_size,
                                std::uint32_t extended_start_sector,
                                std::uint32_t extended_sector_count) {
    const std::uintmax_t extended_start_byte =
        static_cast<std::uintmax_t>(extended_start_sector) * sector_size;
    const std::uintmax_t extended_length_bytes =
        static_cast<std::uintmax_t>(extended_sector_count) * sector_size;
    const std::uintmax_t extended_end_byte = extended_start_byte + extended_length_bytes;

    std::cout << "  Status: extended partition; reading EBR chain\n";
    std::vector<std::uint64_t> visited_ebr_sectors;
    std::uint64_t current_ebr_sector = extended_start_sector;
    constexpr std::size_t maximum_ebr_records = 128;

    for (std::size_t record = 0; record < maximum_ebr_records; ++record) {
        if (std::find(visited_ebr_sectors.begin(), visited_ebr_sectors.end(),
                      current_ebr_sector) != visited_ebr_sectors.end()) {
            std::cout << "  Warning: EBR chain contains a loop at sector "
                      << current_ebr_sector << '\n';
            return;
        }
        visited_ebr_sectors.push_back(current_ebr_sector);

        const std::uintmax_t ebr_start_byte = current_ebr_sector * sector_size;
        if (!is_valid_range(file_size, ebr_start_byte, sector_size)
            || ebr_start_byte < extended_start_byte
            || ebr_start_byte + sector_size > extended_end_byte) {
            std::cout << "  Warning: EBR at sector " << current_ebr_sector
                      << " lies outside the extended partition\n";
            return;
        }

        std::array<unsigned char, sector_size> ebr{};
        if (!read_sector(image, file_size, current_ebr_sector, ebr)) {
            std::cout << "  Warning: could not read EBR at sector "
                      << current_ebr_sector << '\n';
            return;
        }
        if (ebr[510] != 0x55 || ebr[511] != 0xaa) {
            std::cout << "  Warning: EBR at sector " << current_ebr_sector
                      << " has no 55 aa signature\n";
            return;
        }

        const MbrEntry logical_entry = read_partition_entry(ebr, 0);
        const MbrEntry next_ebr_entry = read_partition_entry(ebr, 1);

        if (logical_entry.type == 0 || logical_entry.sector_count == 0) {
            std::cout << "  Warning: EBR at sector " << current_ebr_sector
                      << " has no valid logical-partition entry\n";
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

        std::cout << "  Logical partition " << record + 1 << ":\n"
                  << "    Type: 0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(logical_entry.type) << std::dec << '\n'
                  << "    Starts: sector " << logical_start_sector
                  << " (byte " << logical_start_byte << ")\n"
                  << "    Length: " << logical_entry.sector_count
                  << " sectors (" << logical_length_bytes << " bytes)\n";

        if (!logical_range_is_valid) {
            std::cout << "    Status: invalid; extends beyond extended-partition boundary\n";
            return;
        }
        std::cout << "    File system: ";
        print_file_system_report(image, file_size, logical_start_byte);

        if (next_ebr_entry.type == 0 && next_ebr_entry.sector_count == 0) {
            std::cout << "  EBR chain complete\n";
            return;
        }
        if (!is_extended_partition(next_ebr_entry.type)
            || next_ebr_entry.sector_count == 0) {
            std::cout << "  Warning: EBR at sector " << current_ebr_sector
                      << " has an invalid next-EBR entry\n";
            return;
        }

        current_ebr_sector = static_cast<std::uint64_t>(extended_start_sector)
            + next_ebr_entry.start_sector;
    }

    std::cout << "  Warning: EBR chain exceeded " << maximum_ebr_records
              << " records; stopped safely\n";
}

void inspect_mbr(std::ifstream& image, std::uintmax_t file_size) {
    if (file_size < sector_size) {
        std::cout << "MBR: image is smaller than one sector; not present\n";
        return;
    }

    std::array<unsigned char, sector_size> first_sector{};
    image.clear();
    image.seekg(0);
    image.read(reinterpret_cast<char*>(first_sector.data()), first_sector.size());
    if (image.gcount() != static_cast<std::streamsize>(first_sector.size())) {
        std::cout << "MBR: could not read the first sector\n";
        return;
    }

    if (first_sector[510] != 0x55 || first_sector[511] != 0xaa) {
        std::cout << "MBR: signature not found\n";
        return;
    }

    std::cout << "MBR: signature found (55 aa); validating partition entries\n";

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
            std::cout << "Partition " << index + 1
                      << ": malformed entry (type and length must both be non-zero)\n";
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
                std::cout << "Warning: partitions " << first.number << " and "
                          << second.number << " overlap\n";
                has_overlap = true;
            }
        }
    }

    if (partitions.empty()) {
        std::cout << "MBR: signature found, but no populated primary entries\n";
        return;
    }

    for (const auto& partition : partitions) {
        std::cout << "Partition " << partition.number << ":\n"
                  << "  Type: 0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(partition.type) << std::dec << '\n'
                  << "  Starts: sector " << partition.start_sector
                  << " (byte " << partition.start_byte << ")\n"
                  << "  Length: " << partition.sector_count
                  << " sectors (" << partition.length_bytes << " bytes)\n";

        if (!partition.range_is_valid) {
            std::cout << "  Status: invalid; extends beyond image boundary\n";
            has_invalid_entry = true;
            continue;
        }

        if (is_extended_partition(partition.type)) {
            inspect_extended_partition(image, file_size, partition.start_sector,
                                       partition.sector_count);
        } else if (partition.type == 0xee) {
            std::cout << "  Status: GPT protective partition\n";
            inspect_gpt(image, file_size);
        } else {
            std::cout << "  File system: ";
            print_file_system_report(image, file_size, partition.start_byte);
        }
    }

    if (!has_invalid_entry && !has_overlap) {
        std::cout << "MBR: likely partition table; all populated primary entries fit the image\n";
    } else {
        std::cout << "MBR: signature found, but entry validation raised warnings\n";
    }
}

int inspect_image_file(const std::filesystem::path& image_path) {
    std::error_code error;
    const bool is_regular_file = std::filesystem::is_regular_file(image_path, error);
    if (error || !is_regular_file) {
        std::cerr << "Error: path is not a readable regular file: "
                  << image_path.string() << '\n';
        return 1;
    }

    const std::uintmax_t file_size = std::filesystem::file_size(image_path, error);
    if (error) {
        std::cerr << "Error: could not determine file size: " << error.message() << '\n';
        return 1;
    }

    std::ifstream image(image_path, std::ios::binary);
    if (!image) {
        std::cerr << "Error: could not open file for reading: "
                  << image_path.string() << '\n';
        return 1;
    }

    constexpr std::size_t preview_limit = 16;
    std::array<unsigned char, preview_limit> preview{};
    const auto preview_size = static_cast<std::streamsize>(
        std::min<std::uintmax_t>(file_size, preview.size()));
    image.read(reinterpret_cast<char*>(preview.data()), preview_size);
    if (image.gcount() != preview_size) {
        std::cerr << "Error: file changed or could not be read completely during inspection.\n";
        return 1;
    }

    std::cout << "Image file: " << image_path.string() << '\n'
              << "Size: " << file_size << " bytes\n"
              << "First " << preview_size << " bytes (hex):";

    for (std::streamsize i = 0; i < preview_size; ++i) {
        std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(preview[static_cast<std::size_t>(i)]);
    }
    std::cout << std::dec << '\n';

    inspect_mbr(image, file_size);
    return 0;
}

std::filesystem::path find_demo_data_directory(const char* executable_path) {
    std::error_code error;
    const std::filesystem::path current_candidate = std::filesystem::current_path(error) / "testdata";
    if (!error && std::filesystem::is_directory(current_candidate, error)) {
        return current_candidate;
    }

    const std::filesystem::path executable = std::filesystem::absolute(executable_path, error);
    if (!error) {
        const std::filesystem::path build_candidate = executable.parent_path().parent_path() / "testdata";
        if (std::filesystem::is_directory(build_candidate, error)) {
            return build_candidate;
        }
    }
    return {};
}

int run_demo(const char* executable_path) {
    const std::filesystem::path test_data = find_demo_data_directory(executable_path);
    if (test_data.empty()) {
        std::cerr << "Error: could not locate the bundled testdata directory.\n"
                  << "Run this command from the FileRelic project directory.\n";
        return 1;
    }

    const std::array<std::pair<std::string_view, std::string_view>, 9> examples{{
        {"Valid MBR layout", "practice-mbr.raw"},
        {"Extended MBR and logical partitions", "practice-mbr-extended.raw"},
        {"Invalid partition-boundary warning", "practice-mbr-out-of-bounds.raw"},
        {"GPT primary/backup validation", "practice-gpt.raw"},
        {"ext4 metadata and root-directory entries", "practice-fs-ext4.raw"},
        {"NTFS filesystem signature", "practice-fs-ntfs.raw"},
        {"APFS filesystem signature", "practice-fs-apfs.raw"},
        {"XFS filesystem signature", "practice-fs-xfs.raw"},
        {"Btrfs filesystem signature", "practice-fs-btrfs.raw"},
    }};

    std::cout << "FileRelic safe demonstration\n"
              << "All images are synthetic and inspected read-only.\n";
    int result = 0;
    for (const auto& [title, filename] : examples) {
        std::cout << "\n=== " << title << " ===\n";
        const auto report = filerelic::inspect_image(test_data / filename);
        filerelic::render_report(report, std::cout);
        if (!report.succeeded()) {
            result = 1;
        }
    }
    return result;
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

    // The parser currently emits a detailed text trace. Capture it here so the
    // CLI and future GUI receive one canonical report object. Later milestones
    // will add typed partition fields alongside this presentation text.
    std::ostringstream captured_output;
    auto* const original_stdout = std::cout.rdbuf(captured_output.rdbuf());
    auto* const original_stderr = std::cerr.rdbuf(captured_output.rdbuf());
    report.exit_code = inspect_image_file(image_path);
    std::cout.rdbuf(original_stdout);
    std::cerr.rdbuf(original_stderr);
    report.rendered_text = captured_output.str();
    return report;
}

void filerelic::render_report(const InspectionReport& report, std::ostream& output) {
    output << report.rendered_text;
}

int main(int argc, char* argv[]) {
    if (argc == 1 || std::string_view(argv[1]) == "--help") {
        print_help();
        return 0;
    }

    if (std::string_view(argv[1]) == "--version") {
        std::cout << FILERELIC_VERSION << '\n';
        return 0;
    }

    if (std::string_view(argv[1]) == "inspect-image" || std::string_view(argv[1]) == "inspect") {
        if (argc != 3) {
            std::cerr << "Usage: filerelic inspect-image <path>\n";
            return 2;
        }
        const auto report = filerelic::inspect_image(argv[2]);
        filerelic::render_report(report, report.succeeded() ? std::cout : std::cerr);
        return report.exit_code;
    }

    if (std::string_view(argv[1]) == "demo") {
        if (argc != 2) {
            std::cerr << "Usage: filerelic demo\n";
            return 2;
        }
        return run_demo(argv[0]);
    }

    std::cerr << "Unknown command: " << argv[1]
              << "\nRun 'filerelic --help' for available commands.\n";
    return 2;
}
