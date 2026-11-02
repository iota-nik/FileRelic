#include "ntfs_adapter.h"

#include "image_inspector.h"

namespace filerelic {

NtfsAdapterStatus ntfs_adapter_status(const std::filesystem::path& image_path) {
    const InspectionReport report = inspect_image(image_path);
    NtfsAdapterStatus status;
    for (const auto& partition : report.partitions) {
        if (partition.file_system.kind == FileSystemKind::ntfs) {
            status.available = true;
            status.capability = "NTFS signature recognized; contract and parser are in progress";
            status.limitation = "This build does not yet enumerate NTFS files or recover deleted content."
                " FileRelic remains signature-detection only for NTFS until a validated adapter is in place.";
            return status;
        }
    }
    status.capability = "No NTFS signature found";
    status.limitation = "The file system is not an NTFS image in the current evidence set.";
    return status;
}

} // namespace filerelic
