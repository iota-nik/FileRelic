#include "volume_access.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <string_view>
#include <system_error>

#include "image_source.h"
#include "sha256.h"

namespace filerelic {
namespace {

struct OpenedVolume {
    std::unique_ptr<FileImageSource> source;
    std::unique_ptr<ExtVolume> volume;
    FileSystemInfo info;
    std::uint64_t partition_number = 0;
    std::string error;
};

// Opens the image, picks the partition, and opens the ext volume inside it.
OpenedVolume open_volume(const std::filesystem::path& image, std::uint64_t partition_number) {
    OpenedVolume opened;
    const InspectionReport report = inspect_image(image);
    if (!report.succeeded()) {
        opened.error = report.diagnostics.empty() ? "could not inspect the image"
                                                  : report.diagnostics.front();
        return opened;
    }

    const PartitionInfo* chosen = nullptr;
    for (const PartitionInfo& partition : report.partitions) {
        if (partition_number == 0) {
            if (partition.range_valid && partition.file_system.kind == FileSystemKind::ext_family) {
                chosen = &partition;
                break;
            }
        } else if (partition.number == partition_number) {
            chosen = &partition;
            break;
        }
    }
    if (chosen == nullptr) {
        opened.error = partition_number == 0
            ? "no ext2/3/4 partition found in this image (run 'filerelic inspect' to see what it contains)"
            : "no partition number " + std::to_string(partition_number) + " in this image";
        return opened;
    }
    if (!chosen->range_valid || chosen->file_system.kind != FileSystemKind::ext_family) {
        opened.error = "partition " + std::to_string(chosen->number)
            + " is not an ext2/3/4 file system that FileRelic can read (found: "
            + chosen->file_system.label + ")";
        return opened;
    }

    std::string error;
    opened.source = FileImageSource::open(image, error);
    if (!opened.source) {
        opened.error = error;
        return opened;
    }
    opened.volume = ExtVolume::open(*opened.source, chosen->start_byte, opened.info, error);
    if (!opened.volume) {
        opened.error = error;
        return opened;
    }
    opened.partition_number = chosen->number;
    return opened;
}

std::string normalize(const std::string& path) {
    return path.empty() || path.front() != '/' ? "/" + path : path;
}

// Turns an on-disk name into a safe single file name for any host OS.
std::string safe_output_name(const std::string& raw) {
    std::string name;
    for (const char character : raw) {
        const auto byte = static_cast<unsigned char>(character);
        const bool bad = byte < 32 || byte == 127 || std::string_view("<>:\"/\\|?*").find(character)
            != std::string_view::npos;
        name.push_back(bad ? '_' : character);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    if (name.empty() || name == "." || name == "..") name = "file";

    std::string stem = name.substr(0, name.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                           "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
                                           "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* word : reserved) {
        if (stem == word) {
            name = "_" + name;
            break;
        }
    }
    if (name.size() > 200) name.resize(200);
    return name;
}

std::filesystem::path unique_path(const std::filesystem::path& directory, const std::string& name) {
    std::error_code error;
    std::filesystem::path candidate = directory / name;
    const auto taken = [&](const std::filesystem::path& path) {
        return std::filesystem::exists(path, error) || std::filesystem::exists(path.string() + ".partial", error);
    };
    if (!taken(candidate)) {
        return candidate;
    }
    const std::filesystem::path original(name);
    const std::string stem = original.stem().string();
    const std::string extension = original.extension().string();
    for (int counter = 1; counter < 10000; ++counter) {
        candidate = directory / (stem + " (" + std::to_string(counter) + ")" + extension);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

ListingResult list_path(const std::filesystem::path& image,
                        std::uint64_t partition_number,
                        const std::string& path) {
    ListingResult result;
    result.path = normalize(path);
    OpenedVolume opened = open_volume(image, partition_number);
    if (!opened.volume) {
        result.error = opened.error;
        return result;
    }
    result.partition_number = opened.partition_number;

    const ExtVolume::Resolved target = opened.volume->resolve(result.path);
    if (!target.ok) {
        result.error = target.error;
        return result;
    }
    result.kind = target.inode.kind;
    if (target.inode.kind == InodeKind::directory) {
        opened.volume->list(target.inode, result.listing);
    } else {
        result.entry.inode_number = target.inode_number;
        result.entry.name = printable_name(target.name);
        result.entry.inode = target.inode;
    }
    result.ok = true;
    return result;
}

FileReadSummary read_file(const std::filesystem::path& image,
                          std::uint64_t partition_number,
                          const std::string& path,
                          const ByteSink& sink) {
    FileReadSummary summary;
    OpenedVolume opened = open_volume(image, partition_number);
    if (!opened.volume) {
        summary.error = opened.error;
        return summary;
    }
    summary.partition_number = opened.partition_number;

    const ExtVolume::Resolved target = opened.volume->resolve(normalize(path));
    if (!target.ok) {
        summary.error = target.error;
        summary.partition_number = 0;       // nothing was read
        return summary;
    }
    if (target.inode.kind != InodeKind::regular_file) {
        summary.partition_number = 0;       // nothing was read
        summary.error = std::string(target.inode.kind == InodeKind::directory
                                        ? "is a directory (extracting directories is not supported yet)"
                                        : "is not a regular file");
        return summary;
    }

    Sha256 hasher;
    const ContentReadResult content = opened.volume->read_content(
        target.inode, [&](const std::uint8_t* data, std::size_t length) {
            hasher.update(data, length);
            return sink(data, length);
        });
    summary.status = content.status;
    summary.error = content.error;
    summary.notes = content.notes;
    summary.expected_size = content.expected_size;
    summary.bytes_delivered = content.bytes_delivered;
    summary.sha256_hex = hasher.finish_hex();
    return summary;
}

ExtractionResult extract_file(const std::filesystem::path& image,
                              std::uint64_t partition_number,
                              const std::string& path,
                              const std::filesystem::path& output_directory) {
    ExtractionResult result;

    std::string leaf;
    for (const char character : normalize(path)) {
        if (character == '/') leaf.clear(); else leaf.push_back(character);
    }
    if (leaf.empty()) {
        result.read.error = "give the path of a file, not '/'";
        return result;
    }

    std::error_code error;
    std::filesystem::create_directories(output_directory, error);
    if (error || !std::filesystem::is_directory(output_directory, error)) {
        result.read.error = "cannot use output directory: " + output_directory.string();
        return result;
    }
    const std::filesystem::path final_path = unique_path(output_directory, safe_output_name(leaf));
    if (final_path.empty()) {
        result.read.error = "could not find a free output file name";
        return result;
    }
    const std::filesystem::path partial_path = final_path.string() + ".partial";
    // Never let the output land on the evidence image itself.
    if (std::filesystem::exists(image, error)
        && (std::filesystem::equivalent(image, final_path, error)
            || std::filesystem::equivalent(image, partial_path, error))) {
        result.read.error = "refusing to write over the source image";
        return result;
    }

    std::ofstream output(partial_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        result.read.error = "could not create output file: " + partial_path.string();
        return result;
    }
    result.read = read_file(image, partition_number, path,
                            [&output](const std::uint8_t* data, std::size_t length) {
                                output.write(reinterpret_cast<const char*>(data),
                                             static_cast<std::streamsize>(length));
                                return static_cast<bool>(output);
                            });
    output.close();
    if (output.fail() && result.read.status == ReadStatus::complete) {
        result.read.status = ReadStatus::failed;
        result.read.error = "the output file could not be written completely";
    }

    if (result.read.status == ReadStatus::complete) {
        std::filesystem::rename(partial_path, final_path, error);
        if (error) {
            result.read.status = ReadStatus::failed;
            result.read.error = "could not finalize output file: " + error.message();
            result.output_path = partial_path;
        } else {
            result.output_path = final_path;
        }
    } else if (result.read.status == ReadStatus::partial) {
        result.output_path = partial_path;          // kept, clearly marked incomplete
    } else {
        std::filesystem::remove(partial_path, error);
    }
    return result;
}

} // namespace filerelic
