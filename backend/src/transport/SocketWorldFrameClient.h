#pragma once

#include "IWorldFrameClient.h"
#include <memory>
#include <string>
#include <vector>

namespace spdlog { class logger; }

namespace caldera::backend::transport {

class SocketWorldFrameClient : public IWorldFrameClient {
public:
    struct Config { std::string endpoint; };
    SocketWorldFrameClient(std::shared_ptr<spdlog::logger> log, Config cfg);
    ~SocketWorldFrameClient() override;

    bool connect(uint32_t timeout_ms) override;
    void disconnect() override;
    std::optional<FrameView> latest(bool verify_checksum) override;
    Stats stats() const override { return stats_; }

private:
    struct WireHeader {
        char     magic[4];
        uint16_t version;
        uint16_t header_bytes;
        uint64_t frame_id;
        uint64_t timestamp_ns;
        uint32_t width;
        uint32_t height;
        uint32_t float_count;
        uint32_t checksum;
        uint32_t checksum_algorithm;
    } __attribute__((packed));

    static bool parseUnixEndpoint(const std::string& ep, std::string& pathOut);
    bool ensureConnected(uint32_t timeout_ms);
    bool readExact(void* buf, size_t bytes);
    bool readHeader(WireHeader& hdr);
    bool readPayload(size_t bytes);

    std::shared_ptr<spdlog::logger> log_;
    Config cfg_{};
    int fd_ = -1;
    std::vector<float> payload_;
    FrameView last_{}, ret_{};
    Stats stats_{};
    bool header_pending_ = true;
    WireHeader hdr_{};
};

} // namespace caldera::backend::transport
