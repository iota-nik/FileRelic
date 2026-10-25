#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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
              << "  filerelic inspect-image <path>\n"
              << "                       Inspect a regular image file read-only\n";
}

std::uint32_t read_little_endian_u32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[3]) << 24U);
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
        return "NTFS signature";
    }
    if (matches_bytes_at(image, file_size, partition_start + 32, "NXSB")) {
        return "APFS container signature";
    }
    if (matches_bytes_at(image, file_size, partition_start, "XFSB")) {
        return "XFS signature";
    }
    if (matches_bytes_at(image, file_size, partition_start + 1024 + 56,
                         std::string_view("\x53\xef", 2))) {
        return "ext-family signature";
    }
    if (matches_bytes_at(image, file_size, partition_start + 0x10040, "_BHRfS_M")) {
        return "Btrfs signature";
    }
    return "no supported signature found";
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
        std::cout << "    File system: "
                  << detect_file_system(image, file_size, logical_start_byte) << '\n';

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
            const bool gpt_header_found = matches_bytes_at(image, file_size, sector_size, "EFI PART");
            std::cout << "  Status: GPT protective partition"
                      << (gpt_header_found ? "; GPT header found" : "; GPT header not found")
                      << '\n';
        } else {
            std::cout << "  File system: "
                      << detect_file_system(image, file_size, partition.start_byte) << '\n';
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

} // namespace

int filerelic::inspect_image(const std::filesystem::path& image_path) {
    return inspect_image_file(image_path);
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

    if (std::string_view(argv[1]) == "inspect-image") {
        if (argc != 3) {
            std::cerr << "Usage: filerelic inspect-image <path>\n";
            return 2;
        }
        return filerelic::inspect_image(argv[2]);
    }

    std::cerr << "Unknown command: " << argv[1]
              << "\nRun 'filerelic --help' for available commands.\n";
    return 2;
}
