#include "Checksum.h"

namespace caldera::backend::common {

namespace {
    uint32_t* crcTable() {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (uint32_t i=0; i<256; ++i) {
                uint32_t c = i;
                for (int k=0;k<8;++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            init = true;
        }
        return table;
    }
}

uint32_t crc32(const float* data, std::size_t count) {
    uint32_t* table = crcTable();
    uint32_t crc = 0xFFFFFFFFu;
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    std::size_t byteCount = count * sizeof(float);
    for (std::size_t i=0;i<byteCount;++i) crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_bytes(const uint8_t* data, std::size_t bytes) {
    uint32_t* table = crcTable();
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i=0;i<bytes;++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

} // namespace caldera::backend::common
