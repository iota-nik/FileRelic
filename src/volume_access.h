#pragma once

// High-level, read-only access to files INSIDE a disk image.
// The CLI (ls / cat / extract) and, later, the GUI call only these functions.
//
// Today only ext2/3/4 volumes are supported. A "partition" is the number shown
// by `filerelic inspect`; 0 means "the first ext-family partition".

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ext_reader.h"
#include "image_inspector.h"

namespace filerelic {

struct ListingResult {
    bool ok = false;
    std::string error;
    std::uint64_t partition_number = 0;
    std::string path;                       // as given, normalized with a leading '/'
    InodeKind kind = InodeKind::unknown;    // kind of the thing at `path`
    DirectoryEntryInfo entry;               // set when `path` is a file (not a directory)
    DirectoryListing listing;               // set when `path` is a directory
};

struct FileReadSummary {
    ReadStatus status = ReadStatus::failed;
    std::string error;
    std::vector<std::string> notes;
    std::uint64_t partition_number = 0;
    std::uint64_t expected_size = 0;
    std::uint64_t bytes_delivered = 0;
    std::string sha256_hex;                 // of the delivered bytes
};

struct ExtractionResult {
    FileReadSummary read;
    std::filesystem::path output_path;      // final file, or the .partial file if incomplete
};

// Lists a directory, or describes a single file.
ListingResult list_path(const std::filesystem::path& image,
                        std::uint64_t partition_number,
                        const std::string& path);

// Streams a file's bytes to `sink` and hashes what was delivered.
FileReadSummary read_file(const std::filesystem::path& image,
                          std::uint64_t partition_number,
                          const std::string& path,
                          const ByteSink& sink);

// Copies one file out of the image into `output_directory`. Never overwrites an
// existing file (a numeric suffix is added), never writes inside the image, and
// writes to "<name>.partial" first; the name is final only when the copy is complete.
ExtractionResult extract_file(const std::filesystem::path& image,
                              std::uint64_t partition_number,
                              const std::string& path,
                              const std::filesystem::path& output_directory);

} // namespace filerelic
