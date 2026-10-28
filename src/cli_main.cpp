#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "image_inspector.h"

#ifndef FILERELIC_VERSION
#define FILERELIC_VERSION "0.0.0"
#endif

namespace {

void print_help() {
    std::cout << "FileRelic " << FILERELIC_VERSION << "\n"
              << "Forensic recovery toolkit (development scaffold)\n\n"
              << "Usage:\n"
              << "  filerelic --version   Print the application version\n"
              << "  filerelic --help      Show this help\n"
              << "  filerelic inspect <path>\n"
              << "                       Inspect a regular image file read-only\n"
              << "  filerelic inspect-image <path>\n"
              << "                       Compatibility name for inspect\n"
              << "  filerelic demo        Run the bundled safe inspection demonstration\n";
}

std::filesystem::path find_demo_data_directory(const char* executable_path) {
    std::error_code error;
    const std::filesystem::path current_candidate = std::filesystem::current_path(error) / "testdata";
    if (!error && std::filesystem::is_directory(current_candidate, error)) {
        return current_candidate;
    }

    const std::filesystem::path executable = std::filesystem::absolute(executable_path, error);
    if (!error) {
        const std::filesystem::path build_candidate = executable.parent_path().parent_path() / "testdata";
        if (std::filesystem::is_directory(build_candidate, error)) {
            return build_candidate;
        }
    }
    return {};
}

int run_demo(const char* executable_path) {
    const std::filesystem::path test_data = find_demo_data_directory(executable_path);
    if (test_data.empty()) {
        std::cerr << "Error: could not locate the bundled testdata directory.\n"
                  << "Run this command from the FileRelic project directory.\n";
        return 1;
    }

    const std::array<std::pair<std::string_view, std::string_view>, 9> examples{{
        {"Valid MBR layout", "practice-mbr.raw"},
        {"Extended MBR and logical partitions", "practice-mbr-extended.raw"},
        {"Invalid partition-boundary warning", "practice-mbr-out-of-bounds.raw"},
        {"GPT primary/backup validation", "practice-gpt.raw"},
        {"ext4 metadata and root-directory entries", "practice-fs-ext4.raw"},
        {"NTFS filesystem signature", "practice-fs-ntfs.raw"},
        {"APFS filesystem signature", "practice-fs-apfs.raw"},
        {"XFS filesystem signature", "practice-fs-xfs.raw"},
        {"Btrfs filesystem signature", "practice-fs-btrfs.raw"},
    }};

    std::cout << "FileRelic safe demonstration\n"
              << "All images are synthetic and inspected read-only.\n";
    int result = 0;
    for (const auto& [title, filename] : examples) {
        std::cout << "\n=== " << title << " ===\n";
        const auto report = filerelic::inspect_image(test_data / filename);
        filerelic::render_report(report, std::cout);
        if (!report.succeeded()) {
            result = 1;
        }
    }
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 1 || std::string_view(argv[1]) == "--help") {
        print_help();
        return 0;
    }

    if (std::string_view(argv[1]) == "--version") {
        std::cout << FILERELIC_VERSION << '\n';
        return 0;
    }

    if (std::string_view(argv[1]) == "inspect-image" || std::string_view(argv[1]) == "inspect") {
        if (argc != 3) {
            std::cerr << "Usage: filerelic inspect-image <path>\n";
            return 2;
        }
        const auto report = filerelic::inspect_image(argv[2]);
        filerelic::render_report(report, report.succeeded() ? std::cout : std::cerr);
        return report.exit_code;
    }

    if (std::string_view(argv[1]) == "demo") {
        if (argc != 2) {
            std::cerr << "Usage: filerelic demo\n";
            return 2;
        }
        return run_demo(argv[0]);
    }

    std::cerr << "Unknown command: " << argv[1]
              << "\nRun 'filerelic --help' for available commands.\n";
    return 2;
}
