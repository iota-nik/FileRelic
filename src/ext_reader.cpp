#include "ext_reader.h"

#include <algorithm>
#include <iomanip>
#include <set>
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
    geometry.inodes_count = le32(superblock + 0);
    geometry.blocks_count = le32(superblock + 4);
    geometry.first_data_block = le32(superblock + 20);
    geometry.blocks_per_group = le32(superblock + 32);
    geometry.has_journal = (compatible_features & 0x4U) != 0;
    geometry.journal_inode = le32(superblock + 0xE0);
    if (geometry.is_64bit) {
        geometry.blocks_count |= static_cast<std::uint64_t>(le32(superblock + 0x150)) << 32U;
    }
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
    return decode_inode(raw.bytes.data());
}

// Decodes one raw inode (geometry_.inode_size bytes): type, size, and where the
// data lives. Used for live inodes and for old copies found in the journal.
InodeInfo ExtVolume::decode_inode(const std::uint8_t* inode) {
    InodeInfo info;
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

// ------------------------------------------------------ deleted entries --

bool ExtVolume::read_raw_inode(std::uint32_t inode_number, RawInode& out) {
    std::uint64_t offset = 0;
    std::string reason;
    if (inode_number == 0 || inode_number > geometry_.inodes_count
        || !locate_inode(inode_number, offset, reason)) {
        return false;
    }
    const ReadResult raw = source_.read(offset, geometry_.inode_size);
    if (!raw.ok()) {
        return false;
    }
    const std::uint8_t* inode = raw.bytes.data();
    out.mode = le16(inode);
    out.dtime = le32(inode + 20);
    out.links_count = le16(inode + 26);
    out.flags = le32(inode + 32);
    return true;
}

// Is this block in use according to its group's block bitmap? A group flagged
// BLOCK_UNINIT has no valid bitmap on disk; nothing but metadata lives there,
// so a data block in it counts as free.
ExtVolume::BlockState ExtVolume::block_state(std::uint64_t block, BitmapCache& cache) {
    if (geometry_.blocks_per_group == 0 || block < geometry_.first_data_block
        || block >= geometry_.blocks_count) {
        return BlockState::unknown;
    }
    const std::uint64_t relative = block - geometry_.first_data_block;
    const std::uint64_t group = relative / geometry_.blocks_per_group;
    const std::uint64_t bit = relative % geometry_.blocks_per_group;

    auto found = cache.find(group);
    if (found == cache.end()) {
        const std::uint64_t table_block = geometry_.block_size == 1024 ? 2 : 1;
        const ReadResult descriptor = source_.read(
            start_ + table_block * geometry_.block_size + group * geometry_.descriptor_size,
            geometry_.descriptor_size);
        if (!descriptor.ok()) {
            return BlockState::unknown;
        }
        const std::uint8_t* bytes_in = descriptor.bytes.data();
        std::uint64_t bitmap_block = le32(bytes_in);
        if (geometry_.is_64bit && geometry_.descriptor_size >= 64) {
            bitmap_block |= static_cast<std::uint64_t>(le32(bytes_in + 0x20)) << 32U;
        }
        const std::uint16_t group_flags = le16(bytes_in + 0x12);
        std::vector<std::uint8_t> bitmap;
        if ((group_flags & 0x2U) != 0) {
            bitmap.assign(geometry_.block_size, 0);          // BLOCK_UNINIT: all data blocks free
        } else {
            if (bitmap_block == 0 || bitmap_block >= geometry_.blocks_count) {
                return BlockState::unknown;
            }
            const ReadResult data = source_.read(start_ + bitmap_block * geometry_.block_size,
                                                 geometry_.block_size);
            if (!data.ok()) {
                return BlockState::unknown;
            }
            bitmap = data.bytes;
        }
        found = cache.emplace(group, std::move(bitmap)).first;
    }
    if (bit / 8 >= found->second.size()) {
        return BlockState::unknown;
    }
    return (found->second[bit / 8] >> (bit % 8)) & 1U ? BlockState::allocated : BlockState::free_block;
}

// Decides how much can honestly be said about one unlinked name, from evidence
// only: the inode's own fields and whether its recorded blocks are in use.
void ExtVolume::judge_deleted(DeletedEntryInfo& entry, BitmapCache& cache) {
    constexpr std::uint64_t max_blocks_checked = 8ULL * 1024 * 1024;
    if (entry.inode_number == 0) {
        entry.status = DeletedStatus::name_only;
        return;
    }
    RawInode raw;
    if (!read_raw_inode(entry.inode_number, raw)) {
        entry.status = DeletedStatus::unknown;
        entry.note = "the inode could not be read";
        return;
    }
    const bool in_use = raw.links_count > 0 && raw.dtime == 0;
    const InodeInfo inode = read_inode(entry.inode_number);
    entry.kind = inode.kind;
    entry.size_bytes = inode.size_bytes;
    if (raw.dtime != 0) {
        entry.deleted_time = raw.dtime;
    }
    if (in_use) {
        entry.status = DeletedStatus::inode_in_use;
        entry.note = "it may be the same file under another name, or a different file that reused the number";
        return;
    }
    if (inode.size_bytes && *inode.size_bytes == 0) {
        entry.status = DeletedStatus::empty_file;
        return;
    }
    if (inode.extents.empty()) {
        if (!inode.limitation.empty() && inode.read && inode.kind != InodeKind::other) {
            entry.status = DeletedStatus::unknown;
            entry.note = inode.limitation;
        } else {
            entry.status = DeletedStatus::block_map_cleared;
            entry.note = "the inode no longer lists any data blocks";
            if (geometry_.has_journal && geometry_.journal_inode != 0) {
                entry.note += "; an older copy of the inode may still be in the journal "
                              "('extract --deleted' looks for it)";
            }
        }
        return;
    }

    std::uint64_t total = 0;
    std::uint64_t allocated = 0;
    std::uint64_t unknown = 0;
    for (const ExtentInfo& extent : inode.extents) {
        for (std::uint32_t index = 0; index < extent.length_blocks; ++index) {
            if (total >= max_blocks_checked) {
                entry.status = DeletedStatus::unknown;
                entry.note = "too many blocks to check; stopped after "
                    + std::to_string(max_blocks_checked);
                return;
            }
            ++total;
            switch (block_state(extent.physical_block + index, cache)) {
            case BlockState::allocated: ++allocated; break;
            case BlockState::unknown: ++unknown; break;
            case BlockState::free_block: break;
            }
        }
    }
    if (unknown != 0) {
        entry.status = DeletedStatus::unknown;
        entry.note = "the inode points outside the volume or at unreadable bitmaps ("
            + std::to_string(unknown) + " of " + std::to_string(total)
            + " blocks); it may have been overwritten";
    } else if (allocated == 0) {
        entry.status = DeletedStatus::likely_intact;
        entry.note = std::to_string(total) + (total == 1 ? " block is" : " blocks are")
            + " free now; free does not mean untouched";
    } else if (allocated == total) {
        entry.status = DeletedStatus::blocks_reused;
    } else {
        entry.status = DeletedStatus::blocks_partly_reused;
        entry.note = std::to_string(allocated) + " of " + std::to_string(total)
            + " blocks are in use again";
    }
    if (!inode.limitation.empty()) {
        entry.note += (entry.note.empty() ? "" : "; ") + inode.limitation;
    }
}

void ExtVolume::list_deleted(const InodeInfo& directory, DeletedListing& listing) {
    listing.attempted = true;
    listing.entries.clear();
    listing.limitation.clear();
    if (directory.kind != InodeKind::directory) {
        listing.limitation = "not a directory";
        return;
    }
    if (!directory.limitation.empty()) {
        listing.limitation = directory.limitation;
        return;
    }

    // Names that exist now are not deleted; collect them so stale copies are skipped.
    std::set<std::pair<std::uint32_t, std::string>> live;
    const std::string live_problem = for_each_entry(directory,
        [&](std::uint32_t inode_number, const std::string& raw_name) {
            live.emplace(inode_number, raw_name);
            return true;
        });
    if (!live_problem.empty()) {
        listing.limitation = live_problem;
    }

    // A directory with a hash index keeps index data in the slack of its first
    // block. Those bytes are not names, so slack after "." / ".." is skipped there.
    bool indexed = false;
    {
        // The directory inode number is not stored in InodeInfo; the flag is read
        // through the first live "." entry.
        std::uint32_t self = 0;
        for_each_entry(directory, [&](std::uint32_t inode_number, const std::string& raw_name) {
            if (raw_name == ".") self = inode_number;
            return false;
        });
        RawInode raw;
        indexed = self != 0 && read_raw_inode(self, raw) && (raw.flags & 0x1000U) != 0;
    }
    if (indexed) {
        listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
            + "indexed directory: names hidden next to \".\" and \"..\" are not examined";
    }

    struct Candidate { std::uint32_t inode_number; std::string raw_name; };
    std::vector<Candidate> found;
    const auto plausible_name = [](const std::uint8_t* name, std::size_t length) {
        for (std::size_t index = 0; index < length; ++index) {
            if (name[index] == 0 || name[index] == '/') return false;
        }
        return length > 0;
    };

    // Scans [begin, end) of a block for entries hidden after a live entry. A removed
    // entry keeps its own header and name; the entry before it simply grew over it.
    const std::function<void(const std::vector<std::uint8_t>&, std::size_t, std::size_t, int)>
        scan_slack = [&](const std::vector<std::uint8_t>& block, std::size_t begin,
                         std::size_t end, int depth) {
            std::size_t position = begin;
            while (depth < 64 && position + 8 <= end) {
                const std::uint8_t* entry = block.data() + position;
                const std::uint32_t inode_number = le32(entry);
                const std::uint16_t record_length = le16(entry + 4);
                const std::size_t name_length = entry[6];
                if (inode_number == 0 || inode_number > geometry_.inodes_count
                    || record_length < 8 || record_length % 4 != 0
                    || record_length > end - position
                    || name_length + 8 > record_length
                    || !plausible_name(entry + 8, name_length)) {
                    return;                                    // cannot tell where the next one starts
                }
                found.push_back({inode_number,
                                 std::string(reinterpret_cast<const char*>(entry + 8), name_length)});
                const std::size_t used = 8 + ((name_length + 3) & ~static_cast<std::size_t>(3));
                scan_slack(block, position + used, position + record_length, depth + 1);
                position += record_length;
            }
        };

    const auto scan_directory_blocks = [&]() {
    std::uint64_t blocks_read = 0;
    for (const ExtentInfo& extent : directory.extents) {
        if (extent.uninitialized) continue;
        for (std::uint32_t block_index = 0; block_index < extent.length_blocks; ++block_index) {
            if (++blocks_read > max_directory_blocks) {
                listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
                    + "directory has more than " + std::to_string(max_directory_blocks)
                    + " blocks; scan truncated";
                return;
            }
            const ReadResult data = source_.read(
                start_ + (extent.physical_block + block_index) * geometry_.block_size,
                geometry_.block_size);
            if (!data.ok()) {
                listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
                    + "could not read a directory block";
                return;
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
                    listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
                        + "invalid directory-entry record; stopped safely";
                    break;
                }
                const std::string raw_name(reinterpret_cast<const char*>(entry + 8), name_length);
                const bool dot_entry = raw_name == "." || raw_name == "..";
                if (inode_number == 0 && name_length > 0
                    && plausible_name(entry + 8, name_length)) {
                    // First entry of a block: removal only zeroes the inode number.
                    found.push_back({0, raw_name});
                }
                if (inode_number != 0 || name_length > 0) {
                    const std::size_t used = 8 + ((name_length + 3) & ~static_cast<std::size_t>(3));
                    if (record_length > used && !(indexed && dot_entry)) {
                        scan_slack(data.bytes, offset + used, offset + record_length, 0);
                    }
                }
                offset += record_length;
            }
        }
    }
    };
    scan_directory_blocks();

    BitmapCache cache;
    std::set<std::pair<std::uint32_t, std::string>> seen;
    for (const Candidate& candidate : found) {
        if (candidate.inode_number != 0 && live.count({candidate.inode_number, candidate.raw_name})) {
            continue;
        }
        if (!seen.emplace(candidate.inode_number, candidate.raw_name).second) {
            continue;
        }
        if (listing.entries.size() >= max_listing_entries) {
            listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
                + "listing truncated after " + std::to_string(max_listing_entries) + " entries";
            break;
        }
        DeletedEntryInfo entry;
        entry.name = printable_name(candidate.raw_name);
        entry.inode_number = candidate.inode_number;
        judge_deleted(entry, cache);
        listing.entries.push_back(std::move(entry));
    }
}

void ExtVolume::list_orphans(OrphanListing& listing) {
    listing.attempted = true;
    listing.entries.clear();
    listing.limitation.clear();

    std::set<std::uint32_t> live_inodes;
    std::set<std::uint32_t> visited_dirs;
    std::vector<std::uint32_t> stack;
    stack.push_back(root_inode_number);
    while (!stack.empty()) {
        const std::uint32_t inode_number = stack.back();
        stack.pop_back();
        if (!visited_dirs.insert(inode_number).second) {
            continue;
        }
        const InodeInfo inode = read_inode(inode_number);
        if (!inode.read || inode.kind != InodeKind::directory) {
            continue;
        }
        const std::string problem = for_each_entry(inode,
            [&](std::uint32_t child_inode, const std::string& raw_name) {
                if (raw_name == "." || raw_name == "..") {
                    return true;
                }
                live_inodes.insert(child_inode);
                const InodeInfo child = read_inode(child_inode);
                if (child.read && child.kind == InodeKind::directory) {
                    stack.push_back(child_inode);
                }
                return true;
            });
        if (!problem.empty()) {
            listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
                + "live-directory scan was incomplete: " + problem;
        }
    }

    for (std::uint32_t inode_number = 1; inode_number <= geometry_.inodes_count; ++inode_number) {
        RawInode raw;
        if (!read_raw_inode(inode_number, raw)) {
            continue;
        }
        if (raw.dtime == 0) {
            continue;
        }
        if (live_inodes.count(inode_number) != 0) {
            continue;
        }

        const InodeInfo inode = read_inode(inode_number);
        if (!inode.read || inode.kind == InodeKind::unknown || inode.kind == InodeKind::other) {
            continue;
        }
        if (listing.entries.size() >= max_listing_entries) {
            listing.limitation += std::string(listing.limitation.empty() ? "" : "; ")
                + "orphan listing truncated after " + std::to_string(max_listing_entries) + " entries";
            break;
        }

        OrphanEntryInfo entry;
        entry.inode_number = inode_number;
        entry.kind = inode.kind;
        entry.size_bytes = inode.size_bytes;
        entry.deleted_time = raw.dtime;
        entry.note = "no live directory entry points at this inode anymore";
        if (!inode.limitation.empty()) {
            entry.note += "; " + inode.limitation;
        }
        listing.entries.push_back(std::move(entry));
    }
}

// ----------------------------------------------------------- journal ------

namespace {
constexpr std::uint32_t jbd2_magic = 0xC03B3998U;
std::uint32_t be32(const std::uint8_t* b) {
    return (static_cast<std::uint32_t>(b[0]) << 24U) | (static_cast<std::uint32_t>(b[1]) << 16U)
         | (static_cast<std::uint32_t>(b[2]) << 8U) | static_cast<std::uint32_t>(b[3]);
}
std::uint16_t be16(const std::uint8_t* b) {
    return static_cast<std::uint16_t>((b[0] << 8U) | b[1]);
}
} // namespace

// Looks through the whole journal area for older copies of the inode-table block
// that holds `inode_number`. When a file is deleted the filesystem often erases
// the inode's block map, but an earlier journaled copy of that inode (from before
// the delete) can still be sitting in the journal. The journal is only read.
// Returns "" on success, otherwise why nothing could be searched.
std::string ExtVolume::find_journal_copies(std::uint32_t inode_number,
                                           std::vector<JournalInodeCopy>& out) {
    out.clear();
    if (!geometry_.has_journal || geometry_.journal_inode == 0) {
        return "this filesystem has no internal journal";
    }
    std::uint64_t inode_offset = 0;
    std::string reason;
    if (!locate_inode(inode_number, inode_offset, reason)) {
        return reason;
    }
    const std::uint64_t relative = inode_offset - start_;
    const std::uint64_t target_block = relative / geometry_.block_size;
    const std::size_t in_block = static_cast<std::size_t>(relative % geometry_.block_size);
    if (in_block + geometry_.inode_size > geometry_.block_size) {
        return "inode does not fit in one block";
    }

    const InodeInfo journal = read_inode(geometry_.journal_inode);
    if (!journal.read || journal.kind != InodeKind::regular_file || journal.extents.empty()
        || !journal.limitation.empty()) {
        return "the journal inode could not be read";
    }
    std::vector<ExtentInfo> extents = journal.extents;
    std::stable_sort(extents.begin(), extents.end(),
                     [](const ExtentInfo& a, const ExtentInfo& b) { return a.logical_block < b.logical_block; });

    // journal block number -> physical block in the filesystem (0 = unmapped)
    const auto map_block = [&](std::uint64_t logical) -> std::uint64_t {
        for (const ExtentInfo& extent : extents) {
            if (!extent.uninitialized && logical >= extent.logical_block
                && logical < extent.logical_block + extent.length_blocks) {
                return extent.physical_block + (logical - extent.logical_block);
            }
        }
        return 0;
    };
    const auto read_journal_block = [&](std::uint64_t logical) {
        const std::uint64_t physical = map_block(logical);
        if (physical == 0 || physical >= geometry_.blocks_count) {
            return ReadResult{{}, std::string("journal block outside the volume")};
        }
        return source_.read(start_ + physical * geometry_.block_size, geometry_.block_size);
    };

    const ReadResult superblock = read_journal_block(0);
    if (!superblock.ok() || be32(superblock.bytes.data()) != jbd2_magic) {
        return "the journal superblock is missing or damaged";
    }
    const std::uint8_t* jsb = superblock.bytes.data();
    const std::uint32_t block_type = be32(jsb + 4);
    if (block_type != 3 && block_type != 4) {
        return "the journal superblock has an unexpected type";
    }
    if (be32(jsb + 12) != geometry_.block_size) {
        return "the journal block size differs from the filesystem's";
    }
    const std::uint64_t max_len = be32(jsb + 16);
    const std::uint64_t first = std::max<std::uint32_t>(be32(jsb + 20), 1);
    const std::uint32_t incompatible = block_type == 4 ? be32(jsb + 0x28) : 0;
    const bool is_64bit = (incompatible & 0x2U) != 0;
    const bool csum_v2 = (incompatible & 0x8U) != 0;
    const bool csum_v3 = (incompatible & 0x10U) != 0;
    const bool has_csum = csum_v2 || csum_v3;
    if (max_len < 2 || max_len > 4ULL * 1024 * 1024) {
        return "the journal size is not plausible";
    }
    const std::size_t tag_bytes = csum_v3 ? 16 : ((csum_v2 ? 14 : 12) - (is_64bit ? 0 : 4));

    std::string problem;
    std::uint64_t logical = first;
    std::uint64_t scanned = 0;
    while (logical < max_len) {
        if (++scanned > max_len + 1) break;
        const ReadResult block = read_journal_block(logical);
        if (!block.ok()) {
            if (map_block(logical) == 0) { ++logical; continue; }
            problem = "part of the journal could not be read";
            ++logical;
            continue;
        }
        const std::uint8_t* header = block.bytes.data();
        if (be32(header) != jbd2_magic || be32(header + 4) != 1) {      // 1 = descriptor block
            ++logical;
            continue;
        }
        const std::uint32_t sequence = be32(header + 8);
        const std::size_t limit = geometry_.block_size - (has_csum ? 4 : 0);
        std::size_t position = 12;
        std::uint64_t tags = 0;
        while (position + tag_bytes <= limit) {
            const std::uint8_t* tag = header + position;
            std::uint64_t target = be32(tag);
            std::uint32_t flags;
            if (csum_v3) {
                flags = be32(tag + 4);
                if (is_64bit) target |= static_cast<std::uint64_t>(be32(tag + 8)) << 32U;
            } else {
                flags = be16(tag + 6);
                if (is_64bit) target |= static_cast<std::uint64_t>(be32(tag + 8)) << 32U;
            }
            position += tag_bytes;
            if ((flags & 0x2U) == 0) position += 16;                    // UUID follows unless SAME_UUID
            if (target == target_block && out.size() < 64) {
                std::uint64_t data_block = logical + 1 + tags;
                if (data_block >= max_len) data_block = data_block - max_len + first;   // log wrapped
                const ReadResult copy = read_journal_block(data_block);
                if (copy.ok()) {
                    std::vector<std::uint8_t> bytes = copy.bytes;
                    if ((flags & 0x1U) != 0) {                            // ESCAPE: magic was zeroed
                        bytes[0] = 0xC0; bytes[1] = 0x3B; bytes[2] = 0x39; bytes[3] = 0x98;
                    }
                    JournalInodeCopy found;
                    found.sequence = sequence;
                    found.journal_block = data_block;
                    const std::uint8_t* raw = bytes.data() + in_block;
                    found.mode = le16(raw);
                    found.links_count = le16(raw + 26);
                    found.dtime = le32(raw + 20);
                    found.inode = decode_inode(raw);
                    out.push_back(std::move(found));
                }
            }
            ++tags;
            if ((flags & 0x8U) != 0) break;                              // LAST_TAG
        }
        logical += 1 + std::max<std::uint64_t>(tags, 0);
    }
    std::stable_sort(out.begin(), out.end(), [](const JournalInodeCopy& a, const JournalInodeCopy& b) {
        return a.sequence > b.sequence;                                  // newest transaction first
    });
    return problem;
}

// ------------------------------------------------------ recovery ----------

ContentReadResult ExtVolume::recover_deleted_content(std::uint32_t inode_number,
                                                     const ByteSink& sink,
                                                     RecoveryDetails& details) {
    constexpr std::uint64_t max_blocks_checked = 8ULL * 1024 * 1024;
    ContentReadResult failure;
    details = RecoveryDetails{};

    RawInode raw;
    if (inode_number == 0 || !read_raw_inode(inode_number, raw)) {
        failure.error = "the inode could not be read";
        return failure;
    }
    if (raw.links_count > 0 && raw.dtime == 0) {
        failure.error = "inode " + std::to_string(inode_number) + " is in use by a file that exists "
            "now; extracting it would copy a different file's data, so FileRelic refuses";
        return failure;
    }
    InodeInfo inode = read_inode(inode_number);
    if (inode.read && (inode.kind == InodeKind::directory || inode.kind == InodeKind::symbolic_link)) {
        failure.error = "only regular files can be recovered (this entry was a "
            + std::string(inode.kind == InodeKind::directory ? "folder" : "link") + ")";
        return failure;
    }
    if (inode.size_bytes && *inode.size_bytes == 0 && inode.kind == InodeKind::regular_file) {
        ContentReadResult empty;
        empty.status = ReadStatus::complete;
        empty.notes.push_back("the deleted file was empty; there was nothing to recover");
        details.source = "the deleted inode";
        return empty;
    }

    details.source = "the deleted inode";
    if (inode.extents.empty()) {
        std::vector<JournalInodeCopy> copies;
        const std::string journal_problem = find_journal_copies(inode_number, copies);
        const JournalInodeCopy* chosen = nullptr;
        for (const JournalInodeCopy& copy : copies) {
            // A usable "before" picture: a live regular file with a block map.
            if (copy.links_count > 0 && copy.dtime == 0 && copy.inode.read
                && copy.inode.kind == InodeKind::regular_file && !copy.inode.extents.empty()
                && copy.inode.size_bytes && *copy.inode.size_bytes > 0) {
                chosen = &copy;
                break;
            }
        }
        if (chosen == nullptr) {
            failure.error = "the filesystem erased where this file's data was, and no usable older "
                "copy of the inode was found in the journal";
            if (!journal_problem.empty()) failure.error += " (" + journal_problem + ")";
            return failure;
        }
        inode = chosen->inode;
        details.from_journal = true;
        details.journal_sequence = chosen->sequence;
        details.source = "an older copy of the inode from the journal (transaction "
            + std::to_string(chosen->sequence) + ")";
    }

    // Blocks that something else owns now must not be copied out as if they were
    // this file's data. They are replaced by zeros (by marking them unwritten),
    // and the result is reported as PARTIAL.
    BitmapCache cache;
    std::vector<ExtentInfo> safe;
    std::uint64_t checked = 0;
    for (const ExtentInfo& extent : inode.extents) {
        if (extent.uninitialized) {
            safe.push_back(extent);
            continue;
        }
        std::uint32_t run_start = 0;
        bool run_usable = true;
        const auto flush = [&](std::uint32_t end) {
            if (end > run_start) {
                ExtentInfo piece = extent;
                piece.logical_block = extent.logical_block + run_start;
                piece.physical_block = extent.physical_block + run_start;
                piece.length_blocks = end - run_start;
                piece.uninitialized = !run_usable;
                safe.push_back(piece);
            }
        };
        for (std::uint32_t index = 0; index < extent.length_blocks; ++index) {
            if (++checked > max_blocks_checked) {
                failure.error = "too many data blocks to check safely";
                return failure;
            }
            const BlockState state = block_state(extent.physical_block + index, cache);
            const bool usable = state == BlockState::free_block;
            ++details.total_blocks;
            if (state == BlockState::allocated) ++details.reused_blocks;
            if (state == BlockState::unknown) ++details.unverifiable_blocks;
            if (index == 0) {
                run_usable = usable;
            } else if (usable != run_usable) {
                flush(index);
                run_start = index;
                run_usable = usable;
            }
        }
        flush(extent.length_blocks);
    }
    inode.extents = std::move(safe);

    ContentReadResult result = read_content(inode, sink);
    if (details.reused_blocks != 0 || details.unverifiable_blocks != 0) {
        result.status = ReadStatus::partial;
        std::string message = std::to_string(details.reused_blocks + details.unverifiable_blocks)
            + " of " + std::to_string(details.total_blocks)
            + " data blocks are in use by other data now (or lie outside the volume) and were "
              "replaced with zeros, not copied";
        result.error = result.error.empty() ? message : result.error + "; " + message;
    }
    return result;
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
