#ifndef CALDERA_BACKEND_COMMON_CHECKSUM_H
#define CALDERA_BACKEND_COMMON_CHECKSUM_H

#include <cstdint>
#include <vector>

namespace caldera::backend::common {

// Simple CRC32 (polynomial 0xEDB88320) implementation for a float data buffer.
uint32_t crc32(const float* data, std::size_t count);
inline uint32_t crc32(const std::vector<float>& v){ return crc32(v.data(), v.size()); }

} // namespace caldera::backend::common

#endif
