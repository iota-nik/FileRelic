#include "image_source.h"

#include <limits>
#include <system_error>

namespace filerelic {

std::unique_ptr<FileImageSource> FileImageSource::open(const std::filesystem::path& path,
                                                       std::string& error) {
    std::error_code ec;
    const bool is_regular = std::filesystem::is_regular_file(path, ec);
    if (ec || !is_regular) {
        error = "Path is not a readable regular file";
        return nullptr;
    }

    const std::uintmax_t file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Could not determine image file size: " + ec.message();
        return nullptr;
    }
    if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
        error = "Image is too large for this platform's file offsets";
        return nullptr;
    }

    // std::ifstream can only read: opening it this way cannot modify the image.
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open image file for reading";
        return nullptr;
    }
    return std::unique_ptr<FileImageSource>(
        new FileImageSource(path, std::move(stream), static_cast<std::uint64_t>(file_size)));
}

ReadResult FileImageSource::read(std::uint64_t offset, std::uint64_t length) {
    ReadResult result;
    if (length > max_read_bytes) {
        result.error = "read of " + std::to_string(length) + " bytes exceeds the "
            + std::to_string(max_read_bytes) + "-byte safety limit";
        return result;
    }
    if (!range_fits(size_, offset, length)) {
        result.error = "read of " + std::to_string(length) + " bytes at offset "
            + std::to_string(offset) + " is outside the image (" + std::to_string(size_)
            + " bytes)";
        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(length));
    if (length == 0) {
        return result;
    }
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    stream_.read(reinterpret_cast<char*>(result.bytes.data()),
                 static_cast<std::streamsize>(length));
    if (stream_.gcount() != static_cast<std::streamsize>(length)) {
        result.bytes.clear();
        result.error = "short read at offset " + std::to_string(offset)
            + " (image changed or could not be read completely)";
    }
    return result;
}

} // namespace filerelic
