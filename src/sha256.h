#pragma once

// SHA-256 (FIPS 180-4), implemented here so the core has no crypto dependency.
// Streaming: call update() any number of times, then finish_hex() once.
//
// A SHA-256 of extracted bytes identifies those bytes. It does NOT prove the
// recovery was complete or matches the original file.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace filerelic {

class Sha256 {
public:
    Sha256();
    void update(const std::uint8_t* data, std::size_t length);
    // Finalizes and returns 64 lowercase hex characters. Call once.
    std::string finish_hex();

private:
    void process_block(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_length_ = 0;
    std::uint64_t total_bytes_ = 0;
};

} // namespace filerelic
