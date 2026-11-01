#pragma once

// Small helpers for reading numbers out of raw bytes.
//
// Disk structures store numbers in "little-endian" order: the least significant
// byte comes first. These helpers rebuild a number from that byte order without
// depending on the CPU we happen to run on.

#include <cstddef>
#include <cstdint>

namespace filerelic::bytes {

inline std::uint16_t le16(const std::uint8_t* b) {
    return static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8U));
}

inline std::uint32_t le32(const std::uint8_t* b) {
    return static_cast<std::uint32_t>(b[0])
        | (static_cast<std::uint32_t>(b[1]) << 8U)
        | (static_cast<std::uint32_t>(b[2]) << 16U)
        | (static_cast<std::uint32_t>(b[3]) << 24U);
}

inline std::uint64_t le64(const std::uint8_t* b) {
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(b[index]) << (index * 8U);
    }
    return value;
}

// CRC-32 (IEEE) as used by GPT headers and partition arrays.
inline std::uint32_t crc32_ieee(const std::uint8_t* b, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= b[index];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            const bool low_bit_set = (crc & 1U) != 0;
            crc >>= 1U;
            if (low_bit_set) {
                crc ^= 0xedb88320U;
            }
        }
    }
    return ~crc;
}

} // namespace filerelic::bytes
