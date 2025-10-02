// SharedMemoryWorldFrameClient: wraps SharedMemoryReader to satisfy IWorldFrameClient interface.

#ifndef CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_WORLD_FRAME_CLIENT_H
#define CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_WORLD_FRAME_CLIENT_H

#include "IWorldFrameClient.h"
#include "SharedMemoryReader.h"
#include <string>
#include <chrono>

namespace caldera::backend::transport {

class SharedMemoryWorldFrameClient : public IWorldFrameClient {
public:
    struct Config {
        std::string shm_name;    // required
        uint32_t max_width = 640;  // capacity (must match server capacity)
        uint32_t max_height = 480; // capacity
    };

    SharedMemoryWorldFrameClient(std::shared_ptr<spdlog::logger> logger, Config cfg);
    ~SharedMemoryWorldFrameClient() override;

    bool connect(uint32_t timeout_ms = 0) override;
    void disconnect() override;
    std::optional<FrameView> latest(bool verify_checksum = true) override;
    Stats stats() const override { return stats_; }

private:
    std::shared_ptr<spdlog::logger> logger_;
    Config cfg_;
    SharedMemoryReader reader_;
    Stats stats_{};
    bool connected_ = false;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_SHARED_MEMORY_WORLD_FRAME_CLIENT_H
