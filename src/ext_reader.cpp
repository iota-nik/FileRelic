#include "ext_reader.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "byte_utils.h"

namespace filerelic {
namespace {

using bytes::le16;
using bytes::le32;

constexpr std::uint32_t root_inode_number = 2;
constexpr std::uint32_t extents_flag = 0x00080000U;
constexpr std::uint32_t inline_data_flag = 0x10000000U;
constexpr std::uint32_t incompat_64bit = 0x80U;
constexpr std::uint16_t extent_magic = 0xF30A;
constexpr std::size_t max_listing_entries = 512;
constexpr std::uint64_t max_directory_blocks = 65536;
constexpr std::size_t max_extents_per_inode = 4096;
constexpr std::uint64_t max_block_map_steps = 4ULL * 1024 * 1024;
constexpr int max_extent_tree_depth = 5;
constexpr std::uint64_t read_chunk_bytes = 1024 * 1024;
// A corrupt inode can claim a multi-terabyte sparse file. Real sparse files exist,
// so zero-filled holes are allowed, but only up to this total per file.
constexpr std::uint64_t max_hole_bytes = 1ULL << 30;   // 1 GiB

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

void append_extent(std::vector<ExtentInfo>& extents, std::uint64_t logical, std::uint64_t physical) {
    if (!extents.empty()) {
        ExtentInfo& last = extents.back();
        if (last.logical_block + last.length_blocks == logical
            && last.physical_block + last.length_blocks == physical && !last.uninitialized
            && last.length_blocks < 32768) {
            ++last.length_blocks;
            return;
        }
    }
    extents.push_back({logical, 1, physical, false});
}

} // namespace

std::string printable_name(const std::string& raw) {
    std::string out;
    std::size_t index = 0;
    while (index < raw.size()) {
        const auto byte = static_cast<std::uint8_t>(raw[index]);
        std::size_t length = 1;
        if (byte < 0x80) {
            out.push_back(byte >= 32 && byte != 127 ? raw[index] : '?');
        } else {
            length = (byte & 0xE0U) == 0xC0U ? 2 : (byte & 0xF0U) == 0xE0U ? 3
                : (byte & 0xF8U) == 0xF0U ? 4 : 0;
            bool valid = length != 0 && index + length <= raw.size();
            for (std::size_t extra = 1; valid && extra < length; ++extra) {
                valid = (static_cast<std::uint8_t>(raw[index + extra]) & 0xC0U) == 0x80U;
            }
            if (valid) {
                out.append(raw, index, length);
            } else {
                out.push_back('?');
                length = 1;
            }
        }
        index += length;
    }
    return out;
}

// ------------------------------------------------------------- open -------

std::unique_ptr<ExtVolume> ExtVolume::open(ReadOnlySource& source,
                                           std::uint64_t partition_start,
                                           FileSystemInfo& info,
                                           std::string& error,
                                           Trace trace) {
    constexpr std::uint64_t superblock_offset = 1024;
    const ReadResult raw = source.read(partition_start + superblock_offset, 1024);
    if (!raw.ok()) {
        error = "could not read the complete ext superblock";
        return nullptr;
    }
    const std::uint8_t* superblock = raw.bytes.data();

    const std::uint32_t log_block_size = le32(superblock + 24);
    if (log_block_size > 6) {
        error = "invalid ext block-size exponent";
        return nullptr;
    }

    const std::uint32_t revision = le32(superblock + 76);
    const std::uint16_t state = le16(superblock + 58);
    // Revision 0 filesystems always use 128-byte inodes; the field is valid from revision 1.
    const std::uint16_t inode_size = revision == 0 ? 128 : le16(superblock + 88);
    const std::uint32_t compatible_features = le32(superblock + 92);
    const std::uint32_t incompatible_features = le32(superblock + 96);

    Geometry geometry;
    geometry.block_size = 1024U << log_block_size;
    geometry.inode_size = inode_size;
    geometry.inodes_per_group = le32(superblock + 40);
    geometry.is_64bit = (incompatible_features & incompat_64bit) != 0;
    if (geometry.is_64bit) {
        geometry.descriptor_size = le16(superblock + 254);
        if (geometry.descriptor_size < 64 || geometry.descriptor_size > 1024) {
            error = "invalid 64-bit group-descriptor size";
            return nullptr;
        }
    }

    info.volume_label = read_ascii_field(superblock + 120, 16);
    info.uuid = format_uuid(superblock + 104);
    info.block_size_bytes = geometry.block_size;
    info.inode_size_bytes = inode_size;
    info.state = state == 1 ? "cleanly unmounted" : state == 2 ? "errors recorded" : "unknown";

    std::string features;
    const auto add_feature = [&features](const char* name) {
        if (!features.empty()) features += ", ";
        features += name;
    };
    const bool has_extents = (incompatible_features & 0x40U) != 0;
    const bool has_journal = (compatible_features & 0x4U) != 0;
    if (has_extents) add_feature("extents");
    if (geometry.is_64bit) add_feature("64-bit");
    if (has_journal) add_feature("journal");
    info.features = features;
    // ext2, ext3 and ext4 share one on-disk family; their feature flags tell them apart.
    info.variant = has_extents ? "ext4" : has_journal ? "ext3" : "ext2";

    if (trace) {
        std::ostringstream line;
        line << "  ext metadata: label=" << info.volume_label << " uuid=" << info.uuid
             << " block=" << geometry.block_size << " inode=" << inode_size
             << " state=" << info.state << " incompatible-mask=0x" << std::hex
             << incompatible_features;
        trace(line.str());
    }
    return std::unique_ptr<ExtVolume>(
        new ExtVolume(source, partition_start, geometry, std::move(trace)));
}

// ------------------------------------------------------------ inodes ------

bool ExtVolume::locate_inode(std::uint32_t inode_number, std::uint64_t& offset, std::string& reason) {
    if (inode_number == 0 || geometry_.inodes_per_group == 0) {
        reason = "invalid inode number";
        return false;
    }
    if (geometry_.inode_size < 128 || geometry_.inode_size > geometry_.block_size) {
        reason = "unsupported inode-table layout";
        return false;
    }
    const std::uint64_t group = (inode_number - 1) / geometry_.inodes_per_group;
    const std::uint64_t index_in_group = (inode_number - 1) % geometry_.inodes_per_group;

    // The group-descriptor table starts in the block after the superblock.
    const std::uint64_t table_block = geometry_.block_size == 1024 ? 2 : 1;
    const std::uint64_t descriptor_offset = start_
        + table_block * geometry_.block_size + group * geometry_.descriptor_size;
    const ReadResult descriptor = source_.read(descriptor_offset, geometry_.descriptor_size);
    if (!descriptor.ok()) {
        reason = "could not read the group descriptor for inode " + std::to_string(inode_number);
        return false;
    }
    std::uint64_t inode_table_block = le32(descriptor.bytes.data() + 8);
    if (geometry_.is_64bit && geometry_.descriptor_size >= 64) {
        inode_table_block |= static_cast<std::uint64_t>(le32(descriptor.bytes.data() + 0x28)) << 32U;
    }
    if (inode_table_block == 0) {
        reason = "unsupported inode-table layout";
        return false;
    }
    offset = start_ + inode_table_block * geometry_.block_size + index_in_group * geometry_.inode_size;
    return true;
}

// An ext4 file's block map is a small tree. A node starts with a 12-byte header
// (magic, entry count, depth). Depth 0 = leaf of extents (runs of blocks);
// depth > 0 = index pointing at child nodes stored in other blocks.
void ExtVolume::walk_extent_node(const std::uint8_t* node, std::size_t node_size, int levels_left,
                                 std::vector<ExtentInfo>& out, std::string& limitation) {
    if (!limitation.empty()) {
        return;
    }
    if (node_size < 12 || le16(node) != extent_magic) {
        limitation = "extent tree node has a bad header";
        return;
    }
    const std::uint16_t entries = le16(node + 2);
    const std::uint16_t depth = le16(node + 6);
    if (static_cast<std::size_t>(entries) * 12 + 12 > node_size) {
        limitation = "extent tree node claims more entries than fit";
        return;
    }
    if (depth > 0 && levels_left <= 0) {
        limitation = "extent tree is deeper than the safety limit";
        return;
    }

    for (std::uint16_t index = 0; index < entries && limitation.empty(); ++index) {
        const std::uint8_t* entry = node + 12 + static_cast<std::size_t>(index) * 12;
        if (depth == 0) {
            if (out.size() >= max_extents_per_inode) {
                limitation = "more than " + std::to_string(max_extents_per_inode)
                    + " extents; list truncated";
                return;
            }
            ExtentInfo extent;
            extent.logical_block = le32(entry);
            std::uint32_t length = le16(entry + 4);
            if (length > 32768) {                // high bit set: allocated but unwritten
                extent.uninitialized = true;
                length -= 32768;
            }
            extent.length_blocks = length;
            extent.physical_block = (static_cast<std::uint64_t>(le16(entry + 6)) << 32U)
                | le32(entry + 8);
            out.push_back(extent);
        } else {
            const std::uint64_t child_block = (static_cast<std::uint64_t>(le16(entry + 8)) << 32U)
                | le32(entry + 4);
            const ReadResult child = source_.read(start_ + child_block * geometry_.block_size,
                                                  geometry_.block_size);
            if (!child.ok()) {
                limitation = "could not read an extent-tree block";
                return;
            }
            walk_extent_node(child.bytes.data(), child.bytes.size(), levels_left - 1, out, limitation);
        }
    }
}

// ext2/ext3 block map: 12 direct pointers, then single/double/triple indirect.
struct ExtVolume::MapState {
    std::vector<ExtentInfo>& out;
    std::string& limitation;
    std::uint64_t logical = 0;
    std::uint64_t total = 0;
    std::uint64_t steps = 0;
};

void ExtVolume::map_block_pointer(MapState& state, std::uint32_t pointer) {
    if (state.logical >= state.total || !state.limitation.empty()) {
        return;
    }
    if (++state.steps > max_block_map_steps) {
        state.limitation = "block map too large to decode; truncated";
        return;
    }
    if (pointer != 0) {
        if (state.out.size() >= max_extents_per_inode) {
            state.limitation = "more than " + std::to_string(max_extents_per_inode)
                + " extents; list truncated";
            return;
        }
        append_extent(state.out, state.logical, pointer);
    }
    ++state.logical;
}

void ExtVolume::map_indirect(MapState& state, std::uint32_t pointer, int level) {
    if (state.logical >= state.total || !state.limitation.empty()) {
        return;
    }
    const std::uint64_t per_block = geometry_.block_size / 4;
    if (pointer == 0) {                          // a hole covering per_block^level blocks
        std::uint64_t span = 1;
        for (int step = 0; step < level; ++step) span *= per_block;
        state.logical += span;
        return;
    }
    const ReadResult block = source_.read(start_ + static_cast<std::uint64_t>(pointer) * geometry_.block_size,
                                          geometry_.block_size);
    if (!block.ok()) {
        state.limitation = "could not read an indirect block";
        return;
    }
    for (std::uint64_t index = 0; index < per_block && state.logical < state.total
                                  && state.limitation.empty(); ++index) {
        const std::uint32_t entry = le32(block.bytes.data() + index * 4);
        if (level == 1) {
            map_block_pointer(state, entry);
        } else {
            map_indirect(state, entry, level - 1);
        }
    }
}

InodeInfo ExtVolume::read_inode(std::uint32_t inode_number) {
    InodeInfo info;
    std::uint64_t inode_offset = 0;
    std::string reason;
    if (!locate_inode(inode_number, inode_offset, reason)) {
        info.limitation = reason;
        return info;
    }
    const ReadResult raw = source_.read(inode_offset, geometry_.inode_size);
    if (!raw.ok()) {
        info.limitation = "could not read inode " + std::to_string(inode_number);
        return info;
    }
    const std::uint8_t* inode = raw.bytes.data();

    const std::uint16_t type = le16(inode) & 0xf000U;
    const std::uint32_t flags = le32(inode + 32);

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

    std::uint64_t size = le32(inode + 4);
    if (info.kind == InodeKind::regular_file && geometry_.inode_size >= 112) {
        size |= static_cast<std::uint64_t>(le32(inode + 108)) << 32U;   // i_size_high
    }
    info.size_bytes = size;

    if (info.kind == InodeKind::symbolic_link) {
        return info;                             // targets are not decoded; links are not followed
    }
    if ((flags & inline_data_flag) != 0) {
        info.limitation = "inline-data files are not supported yet";
        return info;
    }

    if ((flags & extents_flag) != 0) {
        info.mapping = DataMapping::extent_tree;
        walk_extent_node(inode + 40, 60, max_extent_tree_depth, info.extents, info.limitation);
    } else {
        info.mapping = DataMapping::direct_blocks;       // classic ext2/ext3 block map
        const std::uint64_t total_blocks = (size + geometry_.block_size - 1) / geometry_.block_size;
        MapState state{info.extents, info.limitation, 0, total_blocks, 0};
        for (std::size_t slot = 0; slot < 12; ++slot) {
            map_block_pointer(state, le32(inode + 40 + slot * 4));
        }
        map_indirect(state, le32(inode + 40 + 12 * 4), 1);
        map_indirect(state, le32(inode + 40 + 13 * 4), 2);
        map_indirect(state, le32(inode + 40 + 14 * 4), 3);
    }

    if (!info.extents.empty()) {
        info.first_data_block = info.extents.front().physical_block;
        info.first_data_byte = static_cast<std::uintmax_t>(start_)
            + static_cast<std::uintmax_t>(info.extents.front().physical_block) * geometry_.block_size;
    }
    return info;
}

// -------------------------------------------------------- directories -----

std::string ExtVolume::for_each_entry(
    const InodeInfo& directory,
    const std::function<bool(std::uint32_t, const std::string&)>& visit) {
    if (directory.kind != InodeKind::directory) {
        return "not a directory";
    }
    if (!directory.limitation.empty()) {
        return directory.limitation;
    }
    std::uint64_t blocks_read = 0;
    for (const ExtentInfo& extent : directory.extents) {
        if (extent.uninitialized) {
            continue;
        }
        for (std::uint32_t block = 0; block < extent.length_blocks; ++block) {
            if (++blocks_read > max_directory_blocks) {
                return "directory has more than " + std::to_string(max_directory_blocks)
                    + " blocks; scan truncated";
            }
            const ReadResult data = source_.read(
                start_ + (extent.physical_block + block) * geometry_.block_size, geometry_.block_size);
            if (!data.ok()) {
                return "could not read a directory block";
            }
            std::size_t offset = 0;
            while (offset + 8 <= data.bytes.size()) {
                const std::uint8_t* entry = data.bytes.data() + offset;
                const std::uint32_t inode_number = le32(entry);
                const std::uint16_t record_length = le16(entry + 4);
                const std::size_t name_length = entry[6];
                if (record_length < 8 || record_length % 4 != 0
                    || record_length > data.bytes.size() - offset
                    || name_length > static_cast<std::size_t>(record_length) - 8) {
                    return "invalid directory-entry record; stopped safely";
                }
                if (inode_number != 0) {
                    const std::string raw_name(reinterpret_cast<const char*>(entry + 8), name_length);
                    if (!visit(inode_number, raw_name)) {
                        return {};
                    }
                }
                offset += record_length;
            }
        }
    }
    return {};
}

void ExtVolume::list(const InodeInfo& directory, DirectoryListing& listing) {
    listing.attempted = true;
    listing.entries.clear();
    listing.limitation.clear();
    trace("  ext directory:");
    const std::string problem = for_each_entry(directory,
        [&](std::uint32_t inode_number, const std::string& raw_name) {
            if (listing.entries.size() >= max_listing_entries) {
                listing.limitation = "listing truncated after "
                    + std::to_string(max_listing_entries) + " entries";
                return false;
            }
            DirectoryEntryInfo item;
            item.inode_number = inode_number;
            item.name = printable_name(raw_name);
            trace("    " + item.name + " (inode " + std::to_string(inode_number) + ")");
            if (raw_name != "." && raw_name != "..") {
                item.inode = read_inode(inode_number);
            }
            listing.entries.push_back(std::move(item));
            return true;
        });
    if (listing.limitation.empty()) {
        listing.limitation = problem;
    }
    if (!listing.limitation.empty()) {
        trace("    Warning: " + listing.limitation);
    }
}

ExtVolume::Resolved ExtVolume::root() {
    Resolved result;
    result.inode_number = root_inode_number;
    result.inode = read_inode(root_inode_number);
    if (!result.inode.read) {
        result.error = result.inode.limitation.empty() ? "could not read the root inode"
                                                       : result.inode.limitation;
        return result;
    }
    if (result.inode.kind != InodeKind::directory) {
        result.error = "the root inode is not a directory";
        return result;
    }
    result.ok = true;
    return result;
}

ExtVolume::Resolved ExtVolume::resolve(const std::string& path) {
    Resolved current = root();
    if (!current.ok) {
        return current;
    }

    std::vector<std::string> components;
    std::string part;
    for (const char character : path) {
        if (character == '/') {
            if (!part.empty()) components.push_back(part);
            part.clear();
        } else {
            part.push_back(character);
        }
    }
    if (!part.empty()) components.push_back(part);

    std::string walked;
    for (const std::string& component : components) {
        walked += "/" + printable_name(component);
        if (current.inode.kind == InodeKind::symbolic_link) {
            return {false, "symbolic links are not followed: " + walked, 0, component, {}};
        }
        if (current.inode.kind != InodeKind::directory) {
            return {false, "not a directory (before " + walked + ")", 0, component, {}};
        }
        std::uint32_t found = 0;
        const std::string problem = for_each_entry(current.inode,
            [&](std::uint32_t inode_number, const std::string& raw_name) {
                if (raw_name == component) {
                    found = inode_number;
                    return false;
                }
                return true;
            });
        if (found == 0) {
            return {false, "no such file or directory: " + walked
                           + (problem.empty() ? "" : " (" + problem + ")"), 0, component, {}};
        }
        current.inode_number = found;
        current.name = component;
        current.inode = read_inode(found);
        if (!current.inode.read) {
            return {false, "could not read inode for " + walked
                           + (current.inode.limitation.empty() ? "" : ": " + current.inode.limitation),
                    found, component, current.inode};
        }
    }
    current.ok = true;
    return current;
}

// ------------------------------------------------------------ content -----

ContentReadResult ExtVolume::read_content(const InodeInfo& file, const ByteSink& sink) {
    ContentReadResult result;
    if (!file.read || file.kind != InodeKind::regular_file || !file.size_bytes) {
        result.error = "not a readable regular file";
        return result;
    }
    const std::uint64_t size = *file.size_bytes;
    const std::uint64_t block_size = geometry_.block_size;
    result.expected_size = size;

    // If the block map could not be fully decoded we can only deliver what is known.
    const bool map_incomplete = !file.limitation.empty();
    if (map_incomplete) {
        result.error = "block map incomplete: " + file.limitation;
    }

    std::vector<ExtentInfo> extents = file.extents;
    std::stable_sort(extents.begin(), extents.end(),
                     [](const ExtentInfo& a, const ExtentInfo& b) { return a.logical_block < b.logical_block; });

    std::uint64_t position = 0;
    std::uint64_t hole_bytes = 0;
    bool sink_failed = false;
    bool read_failed = false;
    const std::vector<std::uint8_t> zeros(static_cast<std::size_t>(read_chunk_bytes), 0);
    const auto emit_zeros = [&](std::uint64_t count) {
        if (hole_bytes + count > max_hole_bytes) {
            result.error = "holes in this file exceed the " + std::to_string(max_hole_bytes >> 20)
                + " MiB safety limit (corrupt size or block map?); stopped";
            read_failed = true;
            return;
        }
        hole_bytes += count;
        while (count > 0 && !sink_failed) {
            const std::uint64_t take = std::min<std::uint64_t>(count, zeros.size());
            if (!sink(zeros.data(), static_cast<std::size_t>(take))) {
                sink_failed = true;
                break;
            }
            position += take;
            result.bytes_delivered += take;
            count -= take;
        }
    };

    // Two extents that claim the same physical blocks would let a corrupt image
    // make us copy the same data over and over.
    {
        std::vector<ExtentInfo> by_physical;
        for (const ExtentInfo& extent : extents) {
            if (!extent.uninitialized) by_physical.push_back(extent);
        }
        std::sort(by_physical.begin(), by_physical.end(),
                  [](const ExtentInfo& a, const ExtentInfo& b) { return a.physical_block < b.physical_block; });
        for (std::size_t index = 1; index < by_physical.size(); ++index) {
            const ExtentInfo& previous = by_physical[index - 1];
            if (previous.physical_block + previous.length_blocks > by_physical[index].physical_block) {
                result.error = "extents overlap in physical blocks (corrupt block map)";
                read_failed = true;
                break;
            }
        }
    }

    bool sparse = false;
    for (const ExtentInfo& extent : extents) {
        if (sink_failed || read_failed) {
            break;
        }
        const std::uint64_t extent_start = extent.logical_block * block_size;
        if (extent_start >= size) {
            break;
        }
        if (extent_start < position) {
            result.error = "overlapping extents in the block map";
            read_failed = true;
            break;
        }
        if (extent_start > position) {
            sparse = true;
            emit_zeros(extent_start - position);                // hole
            if (read_failed) {
                break;
            }
        }
        const std::uint64_t length = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(extent.length_blocks) * block_size, size - extent_start);
        if (extent.uninitialized) {
            sparse = true;
            emit_zeros(length);
            continue;                                           // loop top stops on failure
        }
        std::uint64_t remaining = length;
        std::uint64_t physical = start_ + extent.physical_block * block_size;
        while (remaining > 0 && !sink_failed) {
            const std::uint64_t take = std::min<std::uint64_t>(remaining, read_chunk_bytes);
            const ReadResult data = source_.read(physical, take);
            if (!data.ok()) {
                result.error = "could not read file data at image offset " + std::to_string(physical);
                read_failed = true;
                break;
            }
            if (!sink(data.bytes.data(), data.bytes.size())) {
                sink_failed = true;
                break;
            }
            position += take;
            result.bytes_delivered += take;
            physical += take;
            remaining -= take;
        }
    }

    if (sink_failed) {
        result.error = "the output could not be written";
        result.status = ReadStatus::failed;
        return result;
    }
    if (result.error.empty() && position < size) {
        sparse = true;
        emit_zeros(size - position);                            // trailing hole
        if (read_failed) {
            result.status = ReadStatus::partial;
            return result;
        }
        if (sink_failed) {
            result.error = "the output could not be written";
            result.status = ReadStatus::failed;
            return result;
        }
    }
    if (sparse) {
        result.notes.push_back("file has holes or unwritten extents; those bytes read as zeros");
    }
    result.status = result.error.empty() && position == size ? ReadStatus::complete
                                                            : ReadStatus::partial;
    return result;
}

// ------------------------------------------------------ scanner glue ------

void read_ext_metadata(ScanContext& context,
                       std::uint64_t partition_start,
                       FileSystemInfo& file_system) {
    std::string error;
    auto volume = ExtVolume::open(context.source, partition_start, file_system, error,
                                  [&context](const std::string& line) { context.trace(line); });
    if (!volume) {
        file_system.limitations.push_back(error);
        context.trace("  ext metadata: " + error);
        return;
    }
    const ExtVolume::Resolved root = volume->root();
    if (!root.ok) {
        file_system.root_directory.attempted = true;
        file_system.root_directory.limitation = root.error;
        context.trace("  ext root directory: " + root.error);
        return;
    }
    volume->list(root.inode, file_system.root_directory);
}

} // namespace filerelic
