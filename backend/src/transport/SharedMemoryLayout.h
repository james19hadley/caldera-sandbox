// SharedMemoryLayout.h
// Unified layout definitions for shared memory transport (version 2)

#ifndef CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_LAYOUT_H
#define CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_LAYOUT_H

#include <cstdint>
#include <cstddef>

namespace caldera::backend::transport::shm {

// Per-buffer metadata (double-buffer design):
struct BufferMeta {
    uint64_t frame_id;        // 0..N monotonically (producer assigned)
    uint64_t timestamp_ns;    // steady_clock production timestamp
    uint32_t width;           // active width in floats
    uint32_t height;          // active height in floats
    uint32_t float_count;     // width * height
    uint32_t checksum;        // CRC32 if algorithm==1 else 0
    uint32_t ready;           // 0 = being written, 1 = valid
};

// Header placed at start of shared memory segment.
struct ShmHeader {
    uint32_t magic;              // 'CALD' 0x43414C44
    uint32_t version;            // layout version (2)
    uint32_t active_index;       // index of buffer to read (0/1)
    uint32_t checksum_algorithm; // 0 = none, 1 = CRC32 (EDB88320)
    BufferMeta buffers[2];       // double buffers
};

static_assert(sizeof(BufferMeta) % 4 == 0, "BufferMeta alignment issue");
static_assert(offsetof(ShmHeader, buffers) % 4 == 0, "ShmHeader buffers alignment");

} // namespace caldera::backend::transport::shm

#endif // CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_LAYOUT_H
