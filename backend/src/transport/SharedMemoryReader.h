#include "SharedMemoryLayout.h"
#ifndef CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_READER_H
#define CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_READER_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace spdlog { class logger; }

namespace caldera::backend::transport {

// Lightweight read-side helper for double-buffer SHM world frame sharing.
class SharedMemoryReader {
public:
    struct FrameView {
        uint64_t frame_id = 0;
        uint64_t timestamp_ns = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        const float* data = nullptr; // points into mapped memory (invalidated after reader destruction)
        uint32_t float_count = 0;
        uint32_t checksum = 0;
        uint32_t checksum_algorithm = 0;
        bool checksum_valid = true; // reader may set false if verification performed & failed
    };

    explicit SharedMemoryReader(std::shared_ptr<spdlog::logger> logger): logger_(std::move(logger)) {}
    ~SharedMemoryReader();

    bool open(const std::string& shm_name, uint32_t max_width, uint32_t max_height);
    void close();

    // Returns latest ready frame view (points to live memory) or nullopt if none.
    std::optional<FrameView> latest();

    // Verify checksum for a frame view (if algorithm recognized). Returns true if either
    // checksum not present (0) or matches. Updates fv.checksum_valid.
    static bool verifyChecksum(FrameView &fv);

private:
    using BufferMeta = caldera::backend::transport::shm::BufferMeta;
    using ShmHeader  = caldera::backend::transport::shm::ShmHeader;

    std::shared_ptr<spdlog::logger> logger_;
    int fd_ = -1;
    void* mapped_ = nullptr;
    size_t mapped_size_ = 0;
    size_t single_buffer_bytes_ = 0;
};

} // namespace caldera::backend::transport

#endif
