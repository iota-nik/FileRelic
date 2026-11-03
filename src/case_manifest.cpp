#include "case_manifest.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "report_format.h"
#include "sha256.h"

#ifndef FILERELIC_VERSION
#define FILERELIC_VERSION "0.1.0"
#endif

namespace filerelic {
namespace {

std::string json_escape(const std::string& input) {
    std::ostringstream out;
    for (const unsigned char ch : input) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    return out.str();
}

std::string iso_utc_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t seconds = system_clock::to_time_t(now);
    std::tm utc_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &seconds);
#else
    gmtime_r(&seconds, &utc_tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    return buffer;
}

std::string sha256_file_hex(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return "";
    }
    filerelic::Sha256 digest;
    char buffer[8192];
    while (input.good()) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize bytes = input.gcount();
        if (bytes > 0) {
            digest.update(reinterpret_cast<const std::uint8_t*>(buffer), static_cast<std::size_t>(bytes));
        }
    }
    return digest.finish_hex();
}

std::string render_partition_json(const PartitionInfo& partition) {
    std::ostringstream out;
    out << "{"
        << "\"number\":" << partition.number << ","
        << "\"name\":" << (partition.name.empty() ? "\"\"" : "\"" + json_escape(partition.name) + "\"") << ","
        << "\"type_code\":" << partition.type_code << ","
        << "\"type_name\":" << "\"" << json_escape(partition.type_id) << "\"" << ","
        << "\"start_sector\":" << partition.start_sector << ","
        << "\"sector_count\":" << partition.sector_count << ","
        << "\"start_byte\":" << partition.start_byte << ","
        << "\"length_bytes\":" << partition.length_bytes << ","
        << "\"range_valid\":" << (partition.range_valid ? "true" : "false") << ","
        << "\"logical\":" << (partition.logical ? "true" : "false") << ","
        << "\"extended\":" << (partition.extended ? "true" : "false") << ","
        << "\"filesystem\":{"
        << "\"kind\":" << "\"" << json_escape(describe_file_system(partition.file_system)) << "\""
        << "}"
        << "}";
    return out.str();
}

std::string render_string_array_json(const std::vector<std::string>& items) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index != 0) out << ",";
        out << "\"" << json_escape(items[index]) << "\"";
    }
    out << "]";
    return out.str();
}

std::string render_actions_json(const std::vector<CaseActionInfo>& actions) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (index != 0) out << ",";
        const CaseActionInfo& action = actions[index];
        out << "{"
            << "\"operation\":\"" << json_escape(action.operation) << "\","
            << "\"source_path\":\"" << json_escape(action.source_path) << "\","
            << "\"status\":\"" << json_escape(action.status) << "\","
            << "\"output_path\":\"" << json_escape(action.output_path) << "\","
            << "\"bytes_written\":";
        if (action.bytes_written) out << *action.bytes_written;
        else out << "null";
        out << ","
            << "\"sha256\":\"" << json_escape(action.sha256) << "\","
            << "\"note\":\"" << json_escape(action.note) << "\""
            << "}";
    }
    out << "]";
    return out.str();
}

std::string render_report_summary(const InspectionReport& report) {
    std::ostringstream out;
    out << "{\n"
        << "  \"title\": \"Summary\",\n"
        << "  \"status\": \"" << (report.succeeded() ? "success" : "failed") << "\",\n"
        << "  \"partition_table\": \"" << (report.partition_table == PartitionTableKind::mbr ? "MBR"
             : report.partition_table == PartitionTableKind::gpt ? "GPT" : "unknown") << "\",\n"
        << "  \"partition_count\": " << report.partitions.size() << ",\n"
        << "  \"warning_count\": " << report.warnings.size() << ",\n"
        << "  \"diagnostic_count\": " << report.diagnostics.size() << "\n"
        << "}";
    return out.str();
}

} // namespace

std::string render_case_manifest_json(const InspectionReport& report,
                                     const CaseManifestOptions& options) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": \"1.0\",\n"
        << "  \"tool\": \"FileRelic\",\n"
        << "  \"tool_version\": \"" << FILERELIC_VERSION << "\",\n"
        << "  \"generated_at\": \"" << iso_utc_now() << "\",\n"
        << "  \"case_name\": \"" << json_escape(options.case_name) << "\",\n"
        << "  \"examiner\": \"" << json_escape(options.examiner) << "\",\n"
        << "  \"output_directory\": \"" << json_escape(options.output_directory) << "\",\n"
        << "  \"image_path\": \"" << json_escape(report.image_path.string()) << "\",\n"
        << "  \"image_size_bytes\": " << report.image_size_bytes << ",\n"
        << "  \"image_sha256\": \"" << json_escape(sha256_file_hex(report.image_path)) << "\",\n"
        << "  \"status\": \"" << (report.succeeded() ? "success" : "failed") << "\",\n"
        << "  \"partition_table\": \"" << (report.partition_table == PartitionTableKind::mbr ? "MBR"
             : report.partition_table == PartitionTableKind::gpt ? "GPT" : "unknown") << "\",\n"
        << "  \"partition_count\": " << report.partitions.size() << ",\n"
        << "  \"summary\": " << render_report_summary(report) << ",\n"
        << "  \"warnings\": " << render_string_array_json(report.warnings) << ",\n"
        << "  \"diagnostics\": " << render_string_array_json(report.diagnostics) << ",\n"
        << "  \"actions\": " << render_actions_json(options.actions) << ",\n"
        << "  \"partitions\": [";
    for (std::size_t index = 0; index < report.partitions.size(); ++index) {
        if (index != 0) out << ",";
        out << render_partition_json(report.partitions[index]);
    }
    out << "]\n"
        << "}\n";
    return out.str();
}

bool write_case_manifest_json(const InspectionReport& report,
                             const std::filesystem::path& destination,
                             const CaseManifestOptions& options) {
    std::filesystem::path output_path = destination;
    if (output_path.empty()) {
        return false;
    }
    std::error_code error;
    if (!output_path.has_parent_path() || output_path.parent_path().empty()) {
        output_path = std::filesystem::current_path(error) / output_path;
    }
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            return false;
        }
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << render_case_manifest_json(report, options);
    output.close();
    return output.good();
}

std::string render_case_report_text(const InspectionReport& report,
                                   const CaseManifestOptions& options) {
    std::ostringstream out;
    if (!options.case_name.empty()) {
        out << "Case: " << options.case_name << '\n';
    }
    if (!options.examiner.empty()) {
        out << "Examiner: " << options.examiner << '\n';
    }
    out << "Generated: " << iso_utc_now() << '\n';
    out << "Image: " << report.image_path.string() << " (" << format_size(report.image_size_bytes)
        << ")\n";
    out << "Status: " << (report.succeeded() ? "success" : "failed") << '\n';
    out << "Partitions: " << report.partitions.size() << '\n';
    if (!report.warnings.empty()) {
        out << "Warnings:\n";
        for (const auto& warning : report.warnings) {
            out << "  - " << warning << '\n';
        }
    }
    if (!report.diagnostics.empty()) {
        out << "Diagnostics:\n";
        for (const auto& diagnostic : report.diagnostics) {
            out << "  - " << diagnostic << '\n';
        }
    }
    return out.str();
}

} // namespace filerelic
