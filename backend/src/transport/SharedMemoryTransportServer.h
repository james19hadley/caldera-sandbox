#ifndef CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_TRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_TRANSPORT_SERVER_H

#include "ITransportServer.h"
#include <memory>
#include <string>
#include <cstdint>
#include <sys/mman.h> // munmap
#include <utility>    // std::swap

namespace spdlog { class logger; }

namespace caldera::backend::transport {

// Simple shared memory writer (single-producer) for WorldFrame height map.
// Not thread-safe beyond single writer usage pattern.
class SharedMemoryTransportServer : public ITransportServer {
public:
    struct Config {
        std::string shm_name = "/caldera_worldframe"; // POSIX shm object name
        uint32_t max_width = 640;  // capacity planning (resizable not yet implemented)
        uint32_t max_height = 480; // capacity planning
        Config() = default;
    };

    SharedMemoryTransportServer(std::shared_ptr<spdlog::logger> logger, Config cfg);
    ~SharedMemoryTransportServer() override;

    void start() override;
    void stop() override;
    void sendWorldFrame(const caldera::backend::common::WorldFrame& frame) override;

private:
    // Version 2: double-buffer header. Two buffers of equal capacity follow header.
    struct BufferMeta {
        uint64_t frame_id = 0;
        uint64_t timestamp_ns = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t float_count = 0;
        uint32_t ready = 0; // 0 = being written / invalid, 1 = valid
    };
    struct ShmHeader {
        uint32_t magic = 0x43414C44; // 'CALD'
        uint32_t version = 2;
        uint32_t active_index = 0; // index of buffer to read (0 or 1)
        uint32_t reserved = 0;
        BufferMeta buffers[2];
    };

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
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_TRANSPORT_SERVER_H
