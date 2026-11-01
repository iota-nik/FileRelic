#include "partition_scanner.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "filesystem_probe.h"

namespace filerelic {
namespace {

using bytes::le32;
using bytes::le64;

constexpr std::size_t sector_size = 512;
constexpr std::size_t partition_entry_offset = 446;
constexpr std::size_t partition_entry_size = 16;
constexpr std::size_t primary_partition_count = 4;

struct MbrEntry {
    std::uint8_t type;
    std::uint32_t start_sector;
    std::uint32_t sector_count;
};

MbrEntry read_partition_entry(const std::vector<std::uint8_t>& sector, std::size_t index) {
    const std::size_t offset = partition_entry_offset + index * partition_entry_size;
    return {sector[offset + 4], le32(sector.data() + offset + 8), le32(sector.data() + offset + 12)};
}

bool is_extended_partition(std::uint8_t type) {
    return type == 0x05 || type == 0x0f || type == 0x85;
}

std::string format_mbr_type_code(std::uint8_t type) {
    std::ostringstream formatted;
    formatted << "0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned int>(type);
    return formatted.str();
}

// Reads one 512-byte sector; empty result means it could not be read.
ReadResult read_sector(ReadOnlySource& source, std::uint64_t sector_number) {
    if (sector_number > std::numeric_limits<std::uint64_t>::max() / sector_size) {
        ReadResult result;
        result.error = "sector number out of range";
        return result;
    }
    return source.read(sector_number * sector_size, sector_size);
}

PartitionInfo make_mbr_partition(std::uint64_t number,
                                 const MbrEntry& entry,
                                 std::uint64_t start_sector,
                                 std::uintmax_t start_byte,
                                 std::uintmax_t length_bytes,
                                 bool range_valid) {
    PartitionInfo partition;
    partition.table = PartitionTableKind::mbr;
    partition.number = number;
    partition.type_code = entry.type;
    partition.type_id = format_mbr_type_code(entry.type);
    partition.start_sector = start_sector;
    partition.sector_count = entry.sector_count;
    partition.start_byte = start_byte;
    partition.length_bytes = length_bytes;
    partition.range_valid = range_valid;
    return partition;
}

// ---------------------------------------------------------------- GPT -----

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

bool read_gpt_header(ReadOnlySource& source, std::uint64_t sector_number,
                     GptHeader& header, std::string& error) {
    const ReadResult read = read_sector(source, sector_number);
    if (!read.ok()) {
        error = "could not read header sector";
        return false;
    }
    const std::vector<std::uint8_t>& sector = read.bytes;
    if (!std::equal(sector.begin(), sector.begin() + 8, "EFI PART")) {
        error = "EFI PART signature not found";
        return false;
    }

    const std::uint32_t header_size = le32(sector.data() + 12);
    if (header_size < 92 || header_size > sector.size()) {
        error = "invalid header size";
        return false;
    }

    const std::uint32_t stored_crc32 = le32(sector.data() + 16);
    std::vector<std::uint8_t> header_for_crc = sector;
    std::fill(header_for_crc.begin() + 16, header_for_crc.begin() + 20, 0);
    if (bytes::crc32_ieee(header_for_crc.data(), header_size) != stored_crc32) {
        error = "header CRC32 does not match";
        return false;
    }

    header = {
        header_size,
        le64(sector.data() + 24),
        le64(sector.data() + 32),
        le64(sector.data() + 40),
        le64(sector.data() + 48),
        le64(sector.data() + 72),
        le32(sector.data() + 80),
        le32(sector.data() + 84),
        le32(sector.data() + 88),
    };
    return true;
}

std::string read_gpt_name(const std::uint8_t* entry, std::size_t entry_size) {
    std::string name;
    for (std::size_t offset = 56; offset + 1 < entry_size && offset < 128; offset += 2) {
        const std::uint8_t low_byte = entry[offset];
        const std::uint8_t high_byte = entry[offset + 1];
        if (low_byte == 0 && high_byte == 0) {
            break;
        }
        name.push_back(high_byte == 0 && low_byte >= 32 && low_byte <= 126
                           ? static_cast<char>(low_byte) : '?');
    }
    return name.empty() ? "unnamed" : name;
}

std::string format_gpt_type_guid(const std::uint8_t* guid) {
    constexpr std::array<std::size_t, 16> order{3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
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

void scan_gpt(ScanContext& context) {
    constexpr std::size_t maximum_partition_entries = 128;
    InspectionReport& report = context.report;
    ReadOnlySource& source = context.source;
    const std::uint64_t total_sectors = source.size() / sector_size;
    report.partition_table = PartitionTableKind::gpt;

    GptHeader primary{};
    std::string error;
    if (!read_gpt_header(source, 1, primary, error)) {
        context.trace("  GPT: primary header invalid; " + error);
        report.partition_table_state = ValidationState::invalid;
        report.warnings.push_back("GPT primary header invalid: " + error);
        return;
    }

    report.partition_table_state = ValidationState::valid;
    context.trace("  GPT: primary header valid (CRC32 checked)");
    bool has_warning = false;
    const auto warn = [&](const std::string& message) {
        context.trace("  GPT warning: " + message);
        report.warnings.push_back("GPT " + message);
        has_warning = true;
    };

    if (primary.current_lba != 1 || primary.backup_lba >= total_sectors
        || primary.first_usable_lba > primary.last_usable_lba) {
        warn("primary header has inconsistent locations or usable range");
    }

    GptHeader backup{};
    if (!read_gpt_header(source, primary.backup_lba, backup, error)) {
        warn("backup header invalid: " + error);
    } else if (backup.current_lba != primary.backup_lba || backup.backup_lba != 1) {
        warn("backup header does not point back to primary header");
    } else {
        context.trace("  GPT: backup header valid (CRC32 checked)");
    }

    if (primary.partition_entry_count == 0 || primary.partition_entry_count > maximum_partition_entries
        || primary.partition_entry_size < 128 || primary.partition_entry_size > 4096) {
        warn("partition-entry count or size is unsupported");
        report.partition_table_state = ValidationState::warning;
        return;
    }

    const std::uint64_t entries_length =
        static_cast<std::uint64_t>(primary.partition_entry_count) * primary.partition_entry_size;
    if (primary.partition_entries_lba > std::numeric_limits<std::uint64_t>::max() / sector_size
        || !range_fits(source.size(), primary.partition_entries_lba * sector_size, entries_length)) {
        warn("partition-entry array extends beyond image boundary");
        report.partition_table_state = ValidationState::invalid;
        return;
    }

    const ReadResult entries_read =
        source.read(primary.partition_entries_lba * sector_size, entries_length);
    if (!entries_read.ok()) {
        warn("could not read complete partition-entry array");
        report.partition_table_state = ValidationState::invalid;
        return;
    }
    const std::vector<std::uint8_t>& entries = entries_read.bytes;
    if (bytes::crc32_ieee(entries.data(), entries.size()) != primary.partition_entries_crc32) {
        warn("partition-entry array CRC32 does not match");
        report.partition_table_state = ValidationState::invalid;
        return;
    }
    context.trace("  GPT: partition-entry array valid (CRC32 checked)");

    struct Range {
        std::uint64_t start_lba;
        std::uint64_t end_lba;
    };
    std::vector<Range> partition_ranges;

    for (std::size_t index = 0; index < primary.partition_entry_count; ++index) {
        const std::uint8_t* entry = entries.data() + index * primary.partition_entry_size;
        const bool is_unused = std::all_of(entry, entry + 16, [](std::uint8_t b) { return b == 0; });
        if (is_unused) {
            continue;
        }

        const std::uint64_t start_lba = le64(entry + 32);
        const std::uint64_t end_lba = le64(entry + 40);
        const bool range_is_valid = start_lba <= end_lba
            && start_lba >= primary.first_usable_lba
            && end_lba <= primary.last_usable_lba
            && end_lba < total_sectors;

        PartitionInfo partition;
        partition.table = PartitionTableKind::gpt;
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

        context.trace("  GPT partition " + std::to_string(index + 1) + " (" + partition.name
                      + "): sectors " + std::to_string(start_lba) + " to " + std::to_string(end_lba));
        if (!range_is_valid) {
            context.trace("    Status: invalid partition range");
            partition.warnings.push_back("Partition range is invalid");
            report.warnings.push_back("GPT partition " + std::to_string(index + 1)
                                      + " has an invalid range");
            report.partitions.push_back(std::move(partition));
            has_warning = true;
            continue;
        }

        partition.file_system = inspect_file_system(context, partition.start_byte);
        report.partitions.push_back(std::move(partition));
        partition_ranges.push_back({start_lba, end_lba});
    }

    for (std::size_t left = 0; left < partition_ranges.size(); ++left) {
        for (std::size_t right = left + 1; right < partition_ranges.size(); ++right) {
            const Range& first = partition_ranges[left];
            const Range& second = partition_ranges[right];
            if (first.start_lba <= second.end_lba && second.start_lba <= first.end_lba) {
                warn("valid partition ranges overlap");
            }
        }
    }

    context.trace(std::string("  GPT: ") + (has_warning ? "layout has warnings" : "likely valid layout"));
    if (has_warning && report.partition_table_state == ValidationState::valid) {
        report.partition_table_state = ValidationState::warning;
    }
}

// ------------------------------------------------- extended MBR (EBR) -----

void scan_extended_partition(ScanContext& context,
                             std::uint32_t extended_start_sector,
                             std::uint32_t extended_sector_count) {
    InspectionReport& report = context.report;
    ReadOnlySource& source = context.source;
    const std::uintmax_t extended_start_byte =
        static_cast<std::uintmax_t>(extended_start_sector) * sector_size;
    const std::uintmax_t extended_length_bytes =
        static_cast<std::uintmax_t>(extended_sector_count) * sector_size;
    const std::uintmax_t extended_end_byte = extended_start_byte + extended_length_bytes;
    const auto record_warning = [&](const std::string& message) {
        context.trace("  Warning: " + message);
        report.warnings.push_back(message);
        report.partition_table_state = ValidationState::warning;
    };

    context.trace("  Status: extended partition; reading EBR chain");
    std::vector<std::uint64_t> visited_ebr_sectors;
    std::uint64_t current_ebr_sector = extended_start_sector;
    constexpr std::size_t maximum_ebr_records = 128;

    for (std::size_t record = 0; record < maximum_ebr_records; ++record) {
        if (std::find(visited_ebr_sectors.begin(), visited_ebr_sectors.end(), current_ebr_sector)
            != visited_ebr_sectors.end()) {
            record_warning("EBR chain contains a loop at sector " + std::to_string(current_ebr_sector));
            return;
        }
        visited_ebr_sectors.push_back(current_ebr_sector);

        const std::uintmax_t ebr_start_byte = current_ebr_sector * sector_size;
        if (!range_fits(source.size(), ebr_start_byte, sector_size)
            || ebr_start_byte < extended_start_byte
            || ebr_start_byte + sector_size > extended_end_byte) {
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " lies outside the extended partition");
            return;
        }

        const ReadResult ebr_read = read_sector(source, current_ebr_sector);
        if (!ebr_read.ok()) {
            record_warning("Could not read EBR at sector " + std::to_string(current_ebr_sector));
            return;
        }
        const std::vector<std::uint8_t>& ebr = ebr_read.bytes;
        if (ebr[510] != 0x55 || ebr[511] != 0xaa) {
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " has no 55 aa signature");
            return;
        }

        const MbrEntry logical_entry = read_partition_entry(ebr, 0);
        const MbrEntry next_ebr_entry = read_partition_entry(ebr, 1);
        if (logical_entry.type == 0 || logical_entry.sector_count == 0) {
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " has no valid logical-partition entry");
            return;
        }

        const std::uint64_t logical_start_sector = current_ebr_sector + logical_entry.start_sector;
        const std::uintmax_t logical_start_byte = logical_start_sector * sector_size;
        const std::uintmax_t logical_length_bytes =
            static_cast<std::uintmax_t>(logical_entry.sector_count) * sector_size;
        const bool logical_range_is_valid =
            range_fits(source.size(), logical_start_byte, logical_length_bytes)
            && logical_start_byte >= extended_start_byte
            && logical_start_byte + logical_length_bytes <= extended_end_byte;

        // Logical partitions are numbered from 5 (Linux convention) so they do
        // not collide with the four primary slots.
        const std::uint64_t logical_number = 5 + record;
        PartitionInfo partition = make_mbr_partition(logical_number, logical_entry,
                                                     logical_start_sector, logical_start_byte,
                                                     logical_length_bytes, logical_range_is_valid);
        partition.logical = true;
        context.trace("  Logical partition " + std::to_string(logical_number) + ": type "
                      + partition.type_id + ", sector " + std::to_string(logical_start_sector)
                      + ", " + std::to_string(logical_entry.sector_count) + " sectors");

        if (!logical_range_is_valid) {
            context.trace("    Status: invalid; extends beyond extended-partition boundary");
            partition.warnings.push_back("Logical partition exceeds the extended-partition boundary");
            report.partitions.push_back(std::move(partition));
            report.warnings.push_back("Logical MBR partition " + std::to_string(logical_number)
                                      + " exceeds the extended-partition boundary");
            report.partition_table_state = ValidationState::warning;
            return;
        }
        partition.file_system = inspect_file_system(context, logical_start_byte);
        report.partitions.push_back(std::move(partition));

        if (next_ebr_entry.type == 0 && next_ebr_entry.sector_count == 0) {
            context.trace("  EBR chain complete");
            return;
        }
        if (!is_extended_partition(next_ebr_entry.type) || next_ebr_entry.sector_count == 0) {
            record_warning("EBR at sector " + std::to_string(current_ebr_sector)
                           + " has an invalid next-EBR entry");
            return;
        }
        current_ebr_sector = static_cast<std::uint64_t>(extended_start_sector)
            + next_ebr_entry.start_sector;
    }

    record_warning("EBR chain exceeded the safe limit of " + std::to_string(maximum_ebr_records)
                   + " records");
}

} // namespace

// ---------------------------------------------------------------- MBR -----

void scan_partitions(ScanContext& context) {
    InspectionReport& report = context.report;
    ReadOnlySource& source = context.source;

    if (source.size() < sector_size) {
        context.trace("MBR: image is smaller than one sector; not present");
        report.diagnostics.push_back("Image is smaller than one sector; no MBR can be present");
        return;
    }

    const ReadResult first = read_sector(source, 0);
    if (!first.ok()) {
        context.trace("MBR: could not read the first sector");
        report.diagnostics.push_back("Could not read the image's first sector");
        return;
    }
    const std::vector<std::uint8_t>& first_sector = first.bytes;

    if (first_sector[510] != 0x55 || first_sector[511] != 0xaa) {
        context.trace("MBR: signature not found");
        report.diagnostics.push_back("No MBR signature found at byte offsets 510–511");
        return;
    }

    context.trace("MBR: signature found (55 aa); validating partition entries");
    report.partition_table = PartitionTableKind::mbr;
    report.partition_table_state = ValidationState::valid;

    struct Slot {
        std::size_t number;
        MbrEntry entry;
        std::uintmax_t start_byte;
        std::uintmax_t length_bytes;
        bool range_is_valid;
    };
    std::vector<Slot> slots;
    bool has_invalid_entry = false;

    for (std::size_t index = 0; index < primary_partition_count; ++index) {
        const MbrEntry entry = read_partition_entry(first_sector, index);
        if (entry.type == 0 && entry.sector_count == 0) {
            continue;
        }
        if (entry.type == 0 || entry.sector_count == 0) {
            context.trace("Partition " + std::to_string(index + 1)
                          + ": malformed entry (type and length must both be non-zero)");
            report.warnings.push_back("MBR partition entry " + std::to_string(index + 1)
                                      + " has only one of type or length set");
            has_invalid_entry = true;
            continue;
        }
        const std::uintmax_t start_byte = static_cast<std::uintmax_t>(entry.start_sector) * sector_size;
        const std::uintmax_t length_bytes = static_cast<std::uintmax_t>(entry.sector_count) * sector_size;
        slots.push_back({index + 1, entry, start_byte, length_bytes,
                         range_fits(source.size(), start_byte, length_bytes)});
    }

    bool has_overlap = false;
    for (std::size_t left = 0; left < slots.size(); ++left) {
        for (std::size_t right = left + 1; right < slots.size(); ++right) {
            const Slot& first_slot = slots[left];
            const Slot& second_slot = slots[right];
            if (!first_slot.range_is_valid || !second_slot.range_is_valid) {
                continue;
            }
            const std::uintmax_t first_end = first_slot.start_byte + first_slot.length_bytes;
            const std::uintmax_t second_end = second_slot.start_byte + second_slot.length_bytes;
            if (first_slot.start_byte < second_end && second_slot.start_byte < first_end) {
                context.trace("Warning: partitions " + std::to_string(first_slot.number) + " and "
                              + std::to_string(second_slot.number) + " overlap");
                report.warnings.push_back("MBR partitions " + std::to_string(first_slot.number)
                                          + " and " + std::to_string(second_slot.number) + " overlap");
                has_overlap = true;
            }
        }
    }

    if (slots.empty()) {
        context.trace("MBR: signature found, but no populated primary entries");
        report.partition_table_state = ValidationState::warning;
        report.warnings.push_back("MBR signature exists, but no primary partition entries are populated");
        return;
    }

    for (const Slot& slot : slots) {
        PartitionInfo partition = make_mbr_partition(slot.number, slot.entry, slot.entry.start_sector,
                                                     slot.start_byte, slot.length_bytes,
                                                     slot.range_is_valid);
        context.trace("Partition " + std::to_string(slot.number) + ": type " + partition.type_id
                      + ", sector " + std::to_string(slot.entry.start_sector) + " (byte "
                      + std::to_string(slot.start_byte) + "), " + std::to_string(slot.entry.sector_count)
                      + " sectors (" + std::to_string(slot.length_bytes) + " bytes)");

        if (!slot.range_is_valid) {
            context.trace("  Status: invalid; extends beyond image boundary");
            partition.warnings.push_back("Partition extends beyond image boundary");
            report.partitions.push_back(std::move(partition));
            report.warnings.push_back("MBR partition " + std::to_string(slot.number)
                                      + " extends beyond image boundary");
            has_invalid_entry = true;
            continue;
        }

        if (is_extended_partition(slot.entry.type)) {
            partition.extended = true;
            report.partitions.push_back(std::move(partition));
            scan_extended_partition(context, slot.entry.start_sector, slot.entry.sector_count);
        } else if (slot.entry.type == 0xee) {
            context.trace("  Status: GPT protective partition");
            scan_gpt(context);
        } else {
            partition.file_system = inspect_file_system(context, slot.start_byte);
            report.partitions.push_back(std::move(partition));
        }
    }

    if (!has_invalid_entry && !has_overlap) {
        context.trace("MBR: likely partition table; all populated primary entries fit the image");
    } else {
        context.trace("MBR: signature found, but entry validation raised warnings");
        report.partition_table_state = ValidationState::warning;
    }
}

} // namespace filerelic
