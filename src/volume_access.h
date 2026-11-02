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

struct DeletedListingResult {
    bool ok = false;
    std::string error;
    std::uint64_t partition_number = 0;
    std::string path;                       // the directory that was examined
    DeletedListing listing;
};

struct FileReadSummary {
    ReadStatus status = ReadStatus::failed;
    std::string error;
    std::vector<std::string> notes;
    std::uint64_t partition_number = 0;
    std::uint64_t expected_size = 0;
    std::uint64_t bytes_delivered = 0;
    std::string sha256_hex;                 // of the delivered bytes
    // Only filled when a DELETED entry was recovered:
    std::uint32_t inode_number = 0;
    std::string recovered_from;             // "the deleted inode" / "an older copy ... journal"
    bool from_journal = false;
    std::uint64_t blocks_total = 0;         // data blocks the block map lists
    std::uint64_t blocks_reused = 0;        // of those, in use by other data now (zero-filled)
};

struct ExtractionResult {
    FileReadSummary read;
    std::filesystem::path output_path;      // final file, or the .partial file if incomplete
};

// Lists a directory, or describes a single file.
ListingResult list_path(const std::filesystem::path& image,
                        std::uint64_t partition_number,
                        const std::string& path);

// Lists names that were unlinked from the directory at `path` but are still on
// disk, each with an honest status. Does not read or recover any file content.
DeletedListingResult list_deleted_path(const std::filesystem::path& image,
                                       std::uint64_t partition_number,
                                       const std::string& path);

// Recovers a DELETED file: `path` is the folder plus the deleted name (for example
// "/docs/b.bin", see `ls --deleted`). `inode_number` picks one entry when several
// deleted names match (0 = not given). Uses the deleted inode's block map or, when
// that was erased, an older copy from the journal. Blocks now owned by other data
// are zero-filled and the result is PARTIAL. Never claims the bytes are the original.
FileReadSummary read_deleted_file(const std::filesystem::path& image,
                                  std::uint64_t partition_number,
                                  const std::string& path,
                                  std::uint32_t inode_number,
                                  const ByteSink& sink);

// extract_file() for a deleted entry (same output rules: never overwrites, writes
// "<name>.partial" first, final name only when complete).
ExtractionResult extract_deleted_file(const std::filesystem::path& image,
                                      std::uint64_t partition_number,
                                      const std::string& path,
                                      std::uint32_t inode_number,
                                      const std::filesystem::path& output_directory);

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
