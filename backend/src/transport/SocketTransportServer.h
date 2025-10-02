// SocketTransportServer.h (Phase 10 skeleton)
// FUTURE: Network transport server for WorldFrame streaming over a Unix domain or TCP socket.
// CURRENT: Placeholder skeleton with TODOs; not yet wired into build or main selection.

#ifndef CALDERA_BACKEND_TRANSPORT_SOCKET_TRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_SOCKET_TRANSPORT_SERVER_H

#include "ITransportServer.h"
#include <memory>
#include <string>
#include <cstdint>
#include <thread>

namespace spdlog { class logger; }

namespace caldera::backend::transport {

class SocketTransportServer : public ITransportServer {
public:
    struct Config {
        std::string endpoint = "unix:/tmp/caldera_worldframe.sock"; // unix:/path or future tcp://host:port
        int backlog = 1; // single subscriber semantics initially
        uint32_t checksum_interval_ms = 0; // auto periodic checksum like SHM (0=disabled)
    };

    SocketTransportServer(std::shared_ptr<spdlog::logger> log, Config cfg);
    ~SocketTransportServer() override;

    void start() override;
    void stop() override;
    void sendWorldFrame(const caldera::backend::common::WorldFrame& frame) override;

private:
    struct WireHeader {
        char     magic[4];      // 'C','A','L','D'
        uint16_t version;       // protocol version (1)
        uint16_t header_bytes;  // sizeof(WireHeader)
        uint64_t frame_id;
        uint64_t timestamp_ns;
        uint32_t width;
        uint32_t height;
        uint32_t float_count;
        uint32_t checksum;          // 0 if absent
        uint32_t checksum_algorithm; // 0 none, 1=CRC32
    } __attribute__((packed));
    static_assert(sizeof(WireHeader) == 4+2+2+8+8+4+4+4+4+4, "WireHeader unexpected padding");

    bool ensureSocket();
    void acceptLoop();
    void closeClient();
    static bool parseUnixEndpoint(const std::string& ep, std::string& pathOut);

    std::shared_ptr<spdlog::logger> logger_;
    Config cfg_;
    int listen_fd_ = -1;
    int client_fd_ = -1;
    bool running_ = false;
    uint64_t last_checksum_compute_ns_ = 0;
    std::thread accept_thread_;
    std::string uds_path_; // extracted path for unix endpoint
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_SOCKET_TRANSPORT_SERVER_H
