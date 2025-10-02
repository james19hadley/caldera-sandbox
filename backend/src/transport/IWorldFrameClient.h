// Phase 10: Client abstraction for consuming WorldFrames from backend transports.
// Initial concrete implementation: SharedMemoryWorldFrameClient (wraps SharedMemoryReader).
// Future: SocketWorldFrameClient providing the same interface.

#ifndef CALDERA_BACKEND_TRANSPORT_IWORLD_FRAME_CLIENT_H
#define CALDERA_BACKEND_TRANSPORT_IWORLD_FRAME_CLIENT_H

#include <cstdint>
#include <optional>
#include <memory>
#include <string>

namespace spdlog { class logger; }

namespace caldera::backend::transport {

class IWorldFrameClient {
public:
    struct FrameView {
        uint64_t frame_id = 0;
        uint64_t timestamp_ns = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        const float* data = nullptr; // points into transport-owned memory
        uint32_t float_count = 0;
        uint32_t checksum = 0;
        uint32_t checksum_algorithm = 0;
        bool checksum_valid = true; // verification status (true until verified & mismatch)
    };

    struct Stats {
        uint64_t frames_observed = 0;      // total latest() calls returning a frame
        uint64_t distinct_frames = 0;      // unique frame_id transitions
        uint64_t checksum_present = 0;     // frames which had checksum!=0
        uint64_t checksum_verified = 0;    // successful checksum verifications
        uint64_t checksum_mismatch = 0;    // failed verifications
        uint64_t last_frame_id = 0;
    };

    virtual ~IWorldFrameClient() = default;
    virtual bool connect(uint32_t timeout_ms = 0) = 0; // timeout_ms=0 -> single attempt
    virtual void disconnect() = 0;
    virtual std::optional<FrameView> latest(bool verify_checksum = true) = 0;
    virtual Stats stats() const = 0;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_IWORLD_FRAME_CLIENT_H
