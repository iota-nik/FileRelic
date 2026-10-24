#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

void print_help() {
    std::cout << "FileRelic " << FILERELIC_VERSION << "\n"
              << "Forensic recovery toolkit (development scaffold)\n\n"
              << "Usage:\n"
              << "  filerelic --version   Print the application version\n"
              << "  filerelic --help      Show this help\n"
              << "  filerelic inspect-image <path>\n"
              << "                       Inspect a regular image file read-only\n";
}

int inspect_image(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: filerelic inspect-image <path>\n";
        return 2;
    }

    const std::filesystem::path image_path(argv[2]);
    std::error_code error;
    const bool is_regular_file = std::filesystem::is_regular_file(image_path, error);
    if (error || !is_regular_file) {
        std::cerr << "Error: path is not a readable regular file: "
                  << image_path.string() << '\n';
        return 1;
    }

    const std::uintmax_t file_size = std::filesystem::file_size(image_path, error);
    if (error) {
        std::cerr << "Error: could not determine file size: " << error.message() << '\n';
        return 1;
    }

    std::ifstream image(image_path, std::ios::binary);
    if (!image) {
        std::cerr << "Error: could not open file for reading: "
                  << image_path.string() << '\n';
        return 1;
    }

    constexpr std::size_t preview_limit = 16;
    std::array<unsigned char, preview_limit> preview{};
    const auto preview_size = static_cast<std::streamsize>(
        std::min<std::uintmax_t>(file_size, preview.size()));
    image.read(reinterpret_cast<char*>(preview.data()), preview_size);
    if (image.gcount() != preview_size) {
        std::cerr << "Error: file changed or could not be read completely during inspection.\n";
        return 1;
    }

    std::cout << "Image file: " << image_path.string() << '\n'
              << "Size: " << file_size << " bytes\n"
              << "First " << preview_size << " bytes (hex):";

    for (std::streamsize i = 0; i < preview_size; ++i) {
        std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(preview[static_cast<std::size_t>(i)]);
    }
    std::cout << std::dec << '\n';
    return 0;
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

    if (std::string_view(argv[1]) == "inspect-image") {
        return inspect_image(argc, argv);
    }

    std::cerr << "Unknown command: " << argv[1]
              << "\nRun 'filerelic --help' for available commands.\n";
    return 2;
}
