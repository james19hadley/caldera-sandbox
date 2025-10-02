#ifndef CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

#include "ITransportServer.h"
#include "FifoManager.h"

namespace caldera::backend::transport {

class LocalTransportServer : public ITransportServer {
public:
    struct Config {
        std::string pipe_s2c = "/tmp/caldera_s2c"; // server -> client JSON & future commands
        std::string pipe_c2s = "/tmp/caldera_c2s"; // client -> server HELLO / heartbeat / telemetry
        int handshake_timeout_ms = 3000;             // wait for HELLO after pipes created
        int max_json_field_len = 1024;               // guard
        int heartbeat_log_throttle_ms = 2000;        // avoid log spam
        int server_stats_interval_ms = 1000;         // 0 = disabled periodic server stats JSON on s2c FIFO
    };

    explicit LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                                  std::shared_ptr<spdlog::logger> handshakeLogger = nullptr);
    LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                         std::shared_ptr<spdlog::logger> handshakeLogger,
                         Config cfg);
    ~LocalTransportServer() override;

    void start() override;
    void stop() override;
    void sendWorldFrame(const caldera::backend::common::WorldFrame& frame) override;

    bool isClientAlive(std::chrono::milliseconds timeout) const; // heartbeat freshness
    bool isHandshakeComplete() const { return handshake_completed_.load(); }

    // Provide a lambda that returns a JSON fragment (single-line or multi-line) with server stats.
    // It will be invoked every server_stats_interval_ms (if >0) after handshake completes.
    void setStatsJsonProvider(std::function<std::string()> provider) { stats_json_provider_ = std::move(provider); }
    // Example usage:
    //   server.setStatsJsonProvider([&]{
    //       auto s = collectStats(); // user-defined
    //       return fmt::format("{{\"type\":\"server_stats\",\"frames_published\":{},\"fps\":{}}}", s.frames_published, s.last_publish_fps);
    //   });

private:
    void workerLoop(); // performs handshake then listens for heartbeats/telemetry
    void removeFifos();

    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::logger> handshake_logger_;
    Config cfg_{};
    std::atomic<bool> running_{false};
    std::thread worker_;

    FifoManager fifo_s2c_{handshake_logger_};
    FifoManager fifo_c2s_{handshake_logger_};

    std::atomic<bool> handshake_completed_{false};
    std::atomic<uint64_t> last_heartbeat_ns_{0};
    std::atomic<uint64_t> last_log_heartbeat_ns_{0};
    std::atomic<uint64_t> last_stats_emit_ns_{0};

    int wfd_s2c_ = -1; // kept open after handshake for periodic stats
    std::function<std::string()> stats_json_provider_{};

    std::string shm_name_a_;
    std::string shm_name_b_;
    size_t shm_size_ = 0;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H
