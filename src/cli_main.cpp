// FileRelic command-line interface.
//
// Shape:  filerelic <command> [options] [arguments]
// Output: results go to stdout; errors and failed inspections go to stderr.
// Exit codes:  0 success   1 the operation failed (e.g. unreadable image)   2 bad usage

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "image_inspector.h"
#include "report_format.h"
#include "volume_access.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifndef FILERELIC_VERSION
#define FILERELIC_VERSION "0.0.0"
#endif

namespace {

constexpr int exit_ok = 0;
constexpr int exit_failed = 1;
constexpr int exit_usage = 2;

void print_help(std::ostream& out) {
    out << "FileRelic " << FILERELIC_VERSION << " - read-only forensic disk-image toolkit\n\n"
        << "Usage:\n"
        << "  filerelic <command> [options] [arguments]\n\n"
        << "Commands:\n"
        << "  inspect <image>   Inspect partitions and file systems in a disk image\n"
        << "  ls <image> [path] List a directory (or describe a file) inside the image\n"
        << "  cat <image> <path>        Print a file's contents from inside the image\n"
        << "  extract <image> <path> <outdir>\n"
        << "                    Copy a file out of the image, with a SHA-256 of the output\n"
        << "  demo              Inspect every bundled practice image (needs ./testdata)\n"
        << "  version           Print the version (also: --version, -V)\n"
        << "  help [command]    Show help (also: --help, -h)\n\n"
        << "Run 'filerelic help <command>' for the options of one command.\n\n"
        << "Exit codes: 0 success, 1 operation failed, 2 usage error.\n"
        << "FileRelic never modifies, mounts, or writes to the image.\n";
}

void print_inspect_help(std::ostream& out) {
    out << "Usage: filerelic inspect [--verbose] <image>\n\n"
        << "Reads a regular image file read-only and prints its partition table,\n"
        << "partition ranges, file-system findings, and warnings.\n\n"
        << "Options:\n"
        << "  -v, --verbose   Also print the scanner's step-by-step trace\n"
        << "  -h, --help      Show this help\n\n"
        << "Example: filerelic inspect testdata/practice-fs-ext4.raw\n"
        << "(The older name 'inspect-image' still works but is deprecated.)\n";
}

void print_demo_help(std::ostream& out) {
    out << "Usage: filerelic demo\n\n"
        << "Runs 'inspect' on the synthetic images in ./testdata (or next to the\n"
        << "build directory). If you generated the real ext4 images with\n"
        << "tools/create_real_ext4_fixture.py, those are included too.\n";
}


void print_ls_help(std::ostream& out) {
    out << "Usage: filerelic ls [-v] [--deleted] [--partition N] <image> [path]\n\n"
        << "Lists a directory inside an ext2/3/4 image (default path: /), or describes\n"
        << "one file. Nothing is mounted or modified.\n\n"
        << "Options:\n"
        << "  -p, --partition N   Partition number from 'filerelic inspect'\n"
        << "                      (default: the first ext2/3/4 partition)\n"
        << "  -d, --deleted       List names that were deleted from the directory but\n"
        << "                      are still on disk. Each gets a status; none is a promise\n"
        << "                      that the file can be recovered.\n"
        << "  -h, --help          Show this help\n\n"
        << "Example: filerelic ls testdata/real-ext4-1024.raw /docs\n"
        << "         filerelic ls --deleted testdata/real-ext4-1024.raw /docs\n";
}

void print_cat_help(std::ostream& out) {
    out << "Usage: filerelic cat [--partition N] <image> <path>\n\n"
        << "Writes the file's bytes to standard output. A status line with the byte\n"
        << "count and SHA-256 goes to standard error, so output can be piped or saved.\n\n"
        << "Example: filerelic cat testdata/real-ext4-1024.raw /hello.txt\n";
}

void print_extract_help(std::ostream& out) {
    out << "Usage: filerelic extract [--partition N] <image> <path> <outdir>\n\n"
        << "Copies one file out of the image into <outdir> (created if needed).\n"
        << "Never overwrites an existing file and never writes into the image. The copy\n"
        << "is written as <name>.partial and renamed only when it is complete.\n"
        << "The SHA-256 identifies the OUTPUT bytes; it does not prove the recovery is\n"
        << "complete or identical to the original file.\n\n"
        << "Example: filerelic extract testdata/real-ext4-1024.raw /hello.txt ./recovered\n";
}

std::filesystem::path find_demo_data_directory(const char* executable_path) {
    std::error_code error;
    const std::filesystem::path current_candidate = std::filesystem::current_path(error) / "testdata";
    if (!error && std::filesystem::is_directory(current_candidate, error)) {
        return current_candidate;
    }

    const std::filesystem::path executable = std::filesystem::absolute(executable_path, error);
    if (!error) {
        // build/filerelic -> project root is two levels up from the executable file.
        const std::filesystem::path build_candidate = executable.parent_path().parent_path() / "testdata";
        if (std::filesystem::is_directory(build_candidate, error)) {
            return build_candidate;
        }
    }
    return {};
}

int run_inspect(const std::vector<std::string_view>& args) {
    bool verbose = false;
    std::string_view path;
    for (const std::string_view argument : args) {
        if (argument == "-h" || argument == "--help") {
            print_inspect_help(std::cout);
            return exit_ok;
        }
        if (argument == "-v" || argument == "--verbose") {
            verbose = true;
        } else if (argument.size() > 1 && argument.front() == '-') {
            std::cerr << "filerelic inspect: unknown option '" << argument << "'\n";
            print_inspect_help(std::cerr);
            return exit_usage;
        } else if (path.empty()) {
            path = argument;
        } else {
            std::cerr << "filerelic inspect: expected one image path, got more than one\n";
            print_inspect_help(std::cerr);
            return exit_usage;
        }
    }
    if (path.empty()) {
        std::cerr << "filerelic inspect: missing <image>\n";
        print_inspect_help(std::cerr);
        return exit_usage;
    }

    const auto report = filerelic::inspect_image(std::filesystem::path(std::string(path)));
    // A failed inspection always includes its trace, so the cause is visible.
    filerelic::render_report(report, report.succeeded() ? std::cout : std::cerr,
                             verbose || !report.succeeded());
    return report.succeeded() ? exit_ok : exit_failed;
}


// Shared option parsing for ls / cat / extract.
struct VolumeArgs {
    std::vector<std::string> positional;
    std::uint64_t partition = 0;
    bool help = false;
    bool verbose = false;
    bool deleted = false;
    std::string error;
};

VolumeArgs parse_volume_args(const std::vector<std::string_view>& args) {
    VolumeArgs parsed;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string_view argument = args[index];
        if (argument == "-h" || argument == "--help") {
            parsed.help = true;
        } else if (argument == "-v" || argument == "--verbose") {
            parsed.verbose = true;
        } else if (argument == "-d" || argument == "--deleted") {
            parsed.deleted = true;
        } else if (argument == "-p" || argument == "--partition") {
            if (index + 1 >= args.size()) {
                parsed.error = "missing number after " + std::string(argument);
                return parsed;
            }
            const std::string value(args[++index]);
            char* end = nullptr;
            const unsigned long long number = std::strtoull(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0' || number == 0) {
                parsed.error = "partition must be a positive number, got '" + value + "'";
                return parsed;
            }
            parsed.partition = number;
        } else if (argument.size() > 1 && argument.front() == '-') {
            parsed.error = "unknown option '" + std::string(argument) + "'";
            return parsed;
        } else {
            parsed.positional.emplace_back(argument);
        }
    }
    return parsed;
}

int usage_error(const std::string& command, const std::string& message,
                void (*help)(std::ostream&)) {
    std::cerr << "filerelic " << command << ": " << message << "\n";
    help(std::cerr);
    return exit_usage;
}

void print_entry_line(const filerelic::DirectoryEntryInfo& entry, bool verbose) {
    const filerelic::InodeInfo& inode = entry.inode;
    const bool folder = inode.kind == filerelic::InodeKind::directory;
    std::ostringstream line;
    line << "  " << std::left << std::setw(7) << filerelic::describe_inode_kind(inode.kind) << ' '
         << std::right << std::setw(11)
         << (folder ? std::string("-")
                    : inode.size_bytes ? filerelic::format_size_short(*inode.size_bytes) : "unknown")
         << "   " << entry.name << (folder ? "/" : "");
    if (verbose) line << "   (inode " << entry.inode_number << ")";
    if (!inode.limitation.empty()) line << "   [" << inode.limitation << "]";
    std::cout << line.str() << '\n';
}

void print_deleted_line(const filerelic::DeletedEntryInfo& entry, bool verbose) {
    const bool folder = entry.kind == filerelic::InodeKind::directory;
    const bool known_kind = entry.kind != filerelic::InodeKind::unknown
        && entry.kind != filerelic::InodeKind::other;
    std::ostringstream line;
    line << "  " << std::left << std::setw(7)
         << (known_kind ? filerelic::describe_inode_kind(entry.kind) : std::string("?")) << ' '
         << std::right << std::setw(11)
         << (folder ? std::string("-")
                    : entry.size_bytes ? filerelic::format_size_short(*entry.size_bytes) : "unknown")
         << "   " << entry.name << (folder ? "/" : "");
    if (verbose) {
        line << "   (inode ";
        if (entry.inode_number == 0) line << "erased"; else line << entry.inode_number;
        line << ")";
    }
    std::cout << line.str() << '\n';
    std::cout << "              -> " << filerelic::describe_deleted_status(entry.status) << '\n';
    if (entry.deleted_time) {
        std::cout << "                 deleted at " << filerelic::format_unix_time_utc(*entry.deleted_time)
                  << " (recorded in the inode)\n";
    }
    if (!entry.note.empty()) std::cout << "                 " << entry.note << '\n';
}

int run_ls_deleted(const VolumeArgs& parsed, const std::string& path) {
    const filerelic::DeletedListingResult result =
        filerelic::list_deleted_path(parsed.positional[0], parsed.partition, path);
    if (!result.ok) {
        std::cerr << "filerelic ls: " << result.error << '\n';
        return exit_failed;
    }
    const std::size_t count = result.listing.entries.size();
    std::cout << result.path << "  -  " << count << (count == 1 ? " deleted name" : " deleted names")
              << " found (partition " << result.partition_number << ")\n";
    for (const auto& entry : result.listing.entries) print_deleted_line(entry, parsed.verbose);
    std::cout << "Note: a deleted name is evidence, not a recovered file. Nothing was read or\n"
              << "      extracted. Only names still inside this directory's blocks are shown;\n"
              << "      deleted files whose names are gone, or older copies in the journal, are not.\n";
    if (!result.listing.limitation.empty()) {
        std::cerr << "Warning: the scan may be incomplete - " << result.listing.limitation << '\n';
        return exit_failed;
    }
    return exit_ok;
}

int run_ls(const std::vector<std::string_view>& args) {
    const VolumeArgs parsed = parse_volume_args(args);
    if (parsed.help) { print_ls_help(std::cout); return exit_ok; }
    if (!parsed.error.empty()) return usage_error("ls", parsed.error, print_ls_help);
    if (parsed.positional.empty() || parsed.positional.size() > 2) {
        return usage_error("ls", "expected <image> and an optional <path>", print_ls_help);
    }
    const std::string path = parsed.positional.size() == 2 ? parsed.positional[1] : "/";
    if (parsed.deleted) return run_ls_deleted(parsed, path);

    const filerelic::ListingResult result =
        filerelic::list_path(parsed.positional[0], parsed.partition, path);
    if (!result.ok) {
        std::cerr << "filerelic ls: " << result.error << '\n';
        return exit_failed;
    }
    if (result.kind == filerelic::InodeKind::directory) {
        std::size_t shown = 0;
        for (const auto& entry : result.listing.entries) {
            if (entry.name != "." && entry.name != "..") ++shown;
        }
        std::cout << result.path << "  -  " << shown << (shown == 1 ? " item" : " items")
                  << " (partition " << result.partition_number << ")\n";
        for (const auto& entry : result.listing.entries) {
            if (entry.name == "." || entry.name == "..") continue;
            print_entry_line(entry, parsed.verbose);
        }
        if (!result.listing.limitation.empty()) {
            std::cerr << "Warning: the listing is incomplete - " << result.listing.limitation << '\n';
            return exit_failed;
        }
    } else {
        std::cout << result.path << "  (partition " << result.partition_number << ")\n";
        print_entry_line(result.entry, parsed.verbose);
    }
    return exit_ok;
}

void print_read_summary(const filerelic::FileReadSummary& summary, std::ostream& out) {
    using filerelic::ReadStatus;
    switch (summary.status) {
    case ReadStatus::complete:
        out << "Result:   Complete - the whole file was read.\n"; break;
    case ReadStatus::partial:
        out << "Result:   PARTIAL - only part of the file could be read; treat it as damaged.\n"; break;
    default:
        out << "Result:   FAILED - the file could not be read.\n"; break;
    }
    out << "Size:     " << filerelic::format_size_short(summary.bytes_delivered) << " read of "
        << filerelic::format_size_short(summary.expected_size) << " expected\n"
        << "SHA-256:  " << summary.sha256_hex << "  (fingerprint of the bytes that were output)\n";
    for (const std::string& note : summary.notes) out << "Note:     " << note << '\n';
    if (!summary.error.empty()) out << "Problem:  " << summary.error << '\n';
}

int run_cat(const std::vector<std::string_view>& args) {
    const VolumeArgs parsed = parse_volume_args(args);
    if (parsed.help) { print_cat_help(std::cout); return exit_ok; }
    if (!parsed.error.empty()) return usage_error("cat", parsed.error, print_cat_help);
    if (parsed.deleted) return usage_error("cat", "--deleted only applies to ls", print_cat_help);
    if (parsed.positional.size() != 2) {
        return usage_error("cat", "expected <image> and <path>", print_cat_help);
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);      // file bytes must not be altered by text mode
#endif
    const filerelic::FileReadSummary summary = filerelic::read_file(
        parsed.positional[0], parsed.partition, parsed.positional[1],
        [](const std::uint8_t* data, std::size_t length) {
            return std::fwrite(data, 1, length, stdout) == length;
        });
    std::fflush(stdout);
    // Nothing was read at all (bad path, wrong partition, ...): just say why.
    if (summary.status == filerelic::ReadStatus::failed && summary.partition_number == 0) {
        std::cerr << "filerelic cat: " << summary.error << '\n';
        return exit_failed;
    }
    print_read_summary(summary, std::cerr);
    return summary.status == filerelic::ReadStatus::complete ? exit_ok : exit_failed;
}

int run_extract(const std::vector<std::string_view>& args) {
    const VolumeArgs parsed = parse_volume_args(args);
    if (parsed.help) { print_extract_help(std::cout); return exit_ok; }
    if (!parsed.error.empty()) return usage_error("extract", parsed.error, print_extract_help);
    if (parsed.deleted) return usage_error("extract", "--deleted only applies to ls", print_extract_help);
    if (parsed.positional.size() != 3) {
        return usage_error("extract", "expected <image> <path> <outdir>", print_extract_help);
    }
    const filerelic::ExtractionResult result = filerelic::extract_file(
        parsed.positional[0], parsed.partition, parsed.positional[1], parsed.positional[2]);
    if (result.read.status == filerelic::ReadStatus::failed && result.output_path.empty()) {
        std::cerr << "filerelic extract: " << result.read.error << '\n';
        return exit_failed;
    }
    std::cout << "Extracted " << parsed.positional[1] << " from partition "
              << result.read.partition_number << "\n"
              << "Saved to: " << result.output_path.string() << '\n';
    print_read_summary(result.read, std::cout);
    std::cout << "\nNote: the SHA-256 fingerprint describes the saved bytes only. It cannot prove\n"
              << "the file matches what the original owner had.\n";
    return result.read.status == filerelic::ReadStatus::complete ? exit_ok : exit_failed;
}

int run_demo(const std::vector<std::string_view>& args, const char* executable_path) {
    for (const std::string_view argument : args) {
        if (argument == "-h" || argument == "--help") {
            print_demo_help(std::cout);
            return exit_ok;
        }
        std::cerr << "filerelic demo: takes no arguments (got '" << argument << "')\n";
        print_demo_help(std::cerr);
        return exit_usage;
    }

    const std::filesystem::path test_data = find_demo_data_directory(executable_path);
    if (test_data.empty()) {
        std::cerr << "Error: could not locate the bundled testdata directory.\n"
                  << "Run this command from the FileRelic project directory.\n";
        return exit_failed;
    }

    std::vector<std::pair<std::string_view, std::string_view>> examples{
        {"Valid MBR layout", "practice-mbr.raw"},
        {"Extended MBR and logical partitions", "practice-mbr-extended.raw"},
        {"Invalid partition-boundary warning", "practice-mbr-out-of-bounds.raw"},
        {"GPT primary/backup validation", "practice-gpt.raw"},
        {"ext4 metadata and root-directory entries (hand-made fixture)", "practice-fs-ext4.raw"},
        {"NTFS filesystem signature", "practice-fs-ntfs.raw"},
        {"APFS filesystem signature", "practice-fs-apfs.raw"},
        {"XFS filesystem signature", "practice-fs-xfs.raw"},
        {"Btrfs filesystem signature", "practice-fs-btrfs.raw"},
    };
    // Generated, optional: real mke2fs images (see tools/create_real_ext4_fixture.py).
    const std::array<std::pair<std::string_view, std::string_view>, 2> optional_examples{{
        {"Real ext4, 1 KiB blocks (extent tree)", "real-ext4-1024.raw"},
        {"Real ext4, 4 KiB blocks (extent tree)", "real-ext4-4096.raw"},
    }};
    for (const auto& example : optional_examples) {
        std::error_code error;
        if (std::filesystem::is_regular_file(test_data / std::string(example.second), error)) {
            examples.push_back(example);
        }
    }

    std::cout << "FileRelic safe demonstration\n"
              << "All images are synthetic and inspected read-only.\n";
    int result = exit_ok;
    for (const auto& [title, filename] : examples) {
        std::cout << "\n=== " << title << " ===\n";
        const auto report = filerelic::inspect_image(test_data / std::string(filename));
        filerelic::render_report(report, std::cout);
        if (!report.succeeded()) {
            result = exit_failed;
        }
    }
    return result;
}

int run_help(const std::vector<std::string_view>& args) {
    if (args.empty()) {
        print_help(std::cout);
        return exit_ok;
    }
    if (args[0] == "inspect" || args[0] == "inspect-image") {
        print_inspect_help(std::cout);
        return exit_ok;
    }
    if (args[0] == "demo") {
        print_demo_help(std::cout);
        return exit_ok;
    }
    if (args[0] == "ls") { print_ls_help(std::cout); return exit_ok; }
    if (args[0] == "cat") { print_cat_help(std::cout); return exit_ok; }
    if (args[0] == "extract") { print_extract_help(std::cout); return exit_ok; }
    std::cerr << "filerelic help: unknown command '" << args[0] << "'\n";
    return exit_usage;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 1) {
        print_help(std::cout);
        return exit_ok;
    }

    const std::string_view command(argv[1]);
    const std::vector<std::string_view> args(argv + 2, argv + argc);

    if (command == "--help" || command == "-h") {
        return run_help({});
    }
    if (command == "help") {
        return run_help(args);
    }
    if (command == "--version" || command == "-V" || command == "version") {
        std::cout << FILERELIC_VERSION << '\n';
        return exit_ok;
    }
    if (command == "inspect") {
        return run_inspect(args);
    }
    if (command == "inspect-image") {
        std::cerr << "note: 'inspect-image' is deprecated; use 'filerelic inspect'\n";
        return run_inspect(args);
    }
    if (command == "ls") {
        return run_ls(args);
    }
    if (command == "cat") {
        return run_cat(args);
    }
    if (command == "extract") {
        return run_extract(args);
    }
    if (command == "demo") {
        return run_demo(args, argv[0]);
    }

    std::cerr << "filerelic: unknown command '" << command << "'\n\n";
    print_help(std::cerr);
    return exit_usage;
}
