#ifndef CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_TRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_TRANSPORT_SERVER_H

#include "ITransportServer.h"
#include "common/SensorResolutions.h"
#include <memory>
#include <string>
#include <cstdint>
#include <sys/mman.h> // munmap
#include <utility>    // std::swap
#include "SharedMemoryLayout.h"

namespace spdlog { class logger; }

namespace caldera::backend::transport {

// Simple shared memory writer (single-producer) for WorldFrame height map.
// Not thread-safe beyond single writer usage pattern.
class SharedMemoryTransportServer : public ITransportServer {
public:
    struct Config {
        std::string shm_name = "/caldera_worldframe"; // POSIX shm object name
        // These are CAPACITY LIMITS, not enforced fixed dimensions. Each frame publishes its
        // own width/height in BufferMeta; if a frame exceeds capacity it is dropped.
        // A future auto-resize policy could remap instead of drop.
        uint32_t max_width = common::Transport::SHM_SINGLE_SENSOR_WIDTH;   // Single sensor default
        uint32_t max_height = common::Transport::SHM_SINGLE_SENSOR_HEIGHT; // Single sensor default
        uint32_t checksum_interval_ms = 0; // 0 = disabled auto checksum (only if frame.checksum != 0)
        Config() = default;
    };

    // Runtime statistics snapshot (single-writer so no atomics needed for correctness in producer thread).
    struct Stats {
        uint64_t frames_attempted = 0;         // Total sendWorldFrame calls
        uint64_t frames_published = 0;          // Successfully written & made active
        uint64_t frames_dropped_capacity = 0;   // Dropped because frame dimensions exceed configured capacity
        uint64_t bytes_written = 0;             // Payload bytes copied (floats * 4)
        double   last_publish_fps = 0.0;        // Approx instantaneous FPS (EWMA) of publishes
        uint64_t frames_verified = 0;           // (Future) optionally updated if reader feeds back verification metrics
    };

    SharedMemoryTransportServer(std::shared_ptr<spdlog::logger> logger, Config cfg);
    ~SharedMemoryTransportServer() override;

    void start() override;
    void stop() override;
    void sendWorldFrame(const caldera::backend::common::WorldFrame& frame) override;

    // Returns a copy of internal counters. Safe to call from tests after stopping producer.
    Stats snapshotStats() const { return stats_; }

private:
    // Version 2: double-buffer header. Two buffers of equal capacity follow header.
    using BufferMeta = caldera::backend::transport::shm::BufferMeta;
    using ShmHeader  = caldera::backend::transport::shm::ShmHeader;

    static constexpr uint32_t kHardMaxWidth  = 2048;
    static constexpr uint32_t kHardMaxHeight = 2048;

    bool ensureMapped();

    std::shared_ptr<spdlog::logger> logger_;
    Config cfg_;
    int fd_ = -1;
    size_t mapped_size_ = 0;
    // RAII memory mapping wrapper (definition duplicated here for linkage simplicity)
    class MemoryMapping {
    public:
        MemoryMapping() = default;
        MemoryMapping(void* ptr, size_t size): ptr_(ptr), size_(size) {}
        MemoryMapping(const MemoryMapping&) = delete;
        MemoryMapping& operator=(const MemoryMapping&) = delete;
        MemoryMapping(MemoryMapping&& other) noexcept { swap(other); }
        MemoryMapping& operator=(MemoryMapping&& other) noexcept { if(this!=&other){ cleanup(); swap(other);} return *this; }
        ~MemoryMapping(){ cleanup(); }
        void reset(void* p=nullptr, size_t s=0){ cleanup(); ptr_=p; size_=s; }
        void* get() const { return ptr_; }
    private:
        void cleanup(){ if(ptr_) { munmap(ptr_, size_); ptr_=nullptr; size_=0; } }
        void swap(MemoryMapping& o) noexcept { std::swap(ptr_, o.ptr_); std::swap(size_, o.size_); }
        void* ptr_ = nullptr; size_t size_ = 0;
    };
    MemoryMapping mapping_;
    size_t single_buffer_bytes_ = 0; // capacity in bytes for one float buffer
    bool running_ = false;
    uint64_t last_checksum_compute_ns_ = 0; // monotonic time of last auto checksum
    mutable Stats stats_{}; // mutable to allow snapshot from const context
    uint64_t last_publish_ts_ns_ = 0; // for instantaneous FPS estimate
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_TRANSPORT_SERVER_H
