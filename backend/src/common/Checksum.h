#ifndef CALDERA_BACKEND_COMMON_CHECKSUM_H
#define CALDERA_BACKEND_COMMON_CHECKSUM_H

#include <cstdint>
#include <vector>

namespace caldera::backend::common {

// Simple CRC32 (polynomial 0xEDB88320) implementations.
uint32_t crc32(const float* data, std::size_t count);
inline uint32_t crc32(const std::vector<float>& v){ return crc32(v.data(), v.size()); }
// Generic raw bytes variant (for auxiliary uses). Implemented in .cpp by reusing same table.
uint32_t crc32_bytes(const uint8_t* data, std::size_t bytes);

} // namespace caldera::backend::common

#endif
