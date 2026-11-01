#pragma once

// Read-only access to the bytes of a disk image.
//
// Everything that looks inside an image (partition scanner, filesystem readers,
// and later a real parser such as The Sleuth Kit) asks a ReadOnlySource for
// bytes instead of opening the file itself. That gives us ONE place where:
//   * every read is bounds-checked,
//   * there is no way to write (the interface has no write method),
//   * a later source (E01 image, physical drive) can be swapped in.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace filerelic {

struct ReadResult {
    std::vector<std::uint8_t> bytes;
    std::optional<std::string> error;

    [[nodiscard]] bool ok() const { return !error.has_value(); }
};

class ReadOnlySource {
public:
    virtual ~ReadOnlySource() = default;

    [[nodiscard]] virtual std::uint64_t size() const = 0;
    // Reads exactly `length` bytes starting at `offset`, or returns an error.
    // A short or out-of-range read is always an error, never silent zeros.
    [[nodiscard]] virtual ReadResult read(std::uint64_t offset, std::uint64_t length) = 0;
    [[nodiscard]] virtual std::string description() const = 0;
};

// True when [offset, offset + length) lies inside a source of `source_size`
// bytes. Written so it cannot overflow, even for huge malformed values.
[[nodiscard]] inline bool range_fits(std::uint64_t source_size,
                                     std::uint64_t offset,
                                     std::uint64_t length) {
    return offset <= source_size && length <= source_size - offset;
}

// A regular image file opened for reading only.
class FileImageSource final : public ReadOnlySource {
public:
    // Largest single read we allow. Stops a corrupt length field from asking for
    // gigabytes of memory.
    static constexpr std::uint64_t max_read_bytes = 16ULL * 1024ULL * 1024ULL;

    // Returns nullptr and fills `error` when the path cannot be used.
    static std::unique_ptr<FileImageSource> open(const std::filesystem::path& path,
                                                 std::string& error);

    [[nodiscard]] std::uint64_t size() const override { return size_; }
    [[nodiscard]] ReadResult read(std::uint64_t offset, std::uint64_t length) override;
    [[nodiscard]] std::string description() const override { return path_.string(); }

private:
    FileImageSource(std::filesystem::path path, std::ifstream stream, std::uint64_t size)
        : path_(std::move(path)), stream_(std::move(stream)), size_(size) {}

    std::filesystem::path path_;
    std::ifstream stream_;
    std::uint64_t size_;
};

} // namespace filerelic
