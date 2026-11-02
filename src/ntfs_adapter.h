#pragma once

#include <filesystem>
#include <string>

namespace filerelic {

struct NtfsAdapterStatus {
    bool available = false;
    std::string capability;
    std::string limitation;
};

// Contract-level adapter status: FileRelic recognizes NTFS signatures at the
// image layer, but this status is intentionally conservative until an actual
// adapter performs inventory and recovery through the shared model.
NtfsAdapterStatus ntfs_adapter_status(const std::filesystem::path& image_path);

} // namespace filerelic
