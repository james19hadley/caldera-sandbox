// Unified test-side client emulating a frontend: control-plane (handshake/stats/heartbeat)
// plus data-plane (world frame streaming via IWorldFrameClient implementations).
// Incremental scope: control-plane over local FIFO pipes (existing TestLocalTransportClient logic),
// data-plane via shared memory (SharedMemoryWorldFrameClient). Socket support TODO.
// This stays in tests/helpers (not production) to avoid promising a stable API.

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <chrono>

#include "transport/IWorldFrameClient.h"
#include "transport/SharedMemoryWorldFrameClient.h"
// Socket client (optional)
#if CALDERA_TRANSPORT_SOCKETS
#include "transport/SocketWorldFrameClient.h"
#endif

namespace spdlog { class logger; }

class TestCalderaClient {
public:
    struct ControlConfig { // FIFO / local transport handshake pipes
        std::string pipe_s2c; // server->client (read)
        std::string pipe_c2s; // client->server (write)
        int handshake_timeout_ms = 2000;
        std::string hello = "HELLO_CALDERA_CLIENT_V1\n";
        bool enable_heartbeat = false;
        int heartbeat_interval_ms = 0; // (not auto-scheduled yet; user/test drives sendHeartbeat())
    };
    struct ShmDataConfig {
        std::string shm_name;
        uint32_t max_width = 640;
        uint32_t max_height = 480;
        bool verify_checksum = true;
        int connect_timeout_ms = 2000;
    };
    struct SocketDataConfig {
        std::string endpoint = "unix:/tmp/caldera_worldframe.sock";
        bool verify_checksum = true;
        int connect_timeout_ms = 2000;
    };

    struct FrameView : public caldera::backend::transport::IWorldFrameClient::FrameView {};

    struct Stats : public caldera::backend::transport::IWorldFrameClient::Stats {
        // Gap / jitter statistics
        uint64_t max_gap = 0;              // largest gap (gap-1) observed
        uint64_t total_skipped = 0;        // Σ(gap-1)
        // Latency samples meta
        uint64_t latency_samples = 0;      // number of latency samples stored
    };
    struct LatencyStats { uint64_t count=0; double mean_ms=0.0; double p95_ms=0.0; double max_ms=0.0; };

    explicit TestCalderaClient(std::shared_ptr<spdlog::logger> log);
    ~TestCalderaClient();

    // Control-plane (FIFO handshake). Returns true on successful JSON handshake parse.
    bool connectControl(const ControlConfig& cfg);
    void disconnectControl();
    bool sendHeartbeat();
    bool sendTelemetry(const std::string& jsonLine);
    // Collect lines containing 'server_stats' up to maxLines or timeout.
    std::vector<std::string> collectServerStats(int maxLines, int timeout_ms);
    const std::string& handshakeJson() const { return handshake_json_; }

    // Data-plane (Shared Memory). Returns true if opened within timeout.
    bool connectData(const ShmDataConfig& cfg);
    bool connectData(const SocketDataConfig& cfg);
    void disconnectData();
    std::optional<FrameView> latest(); // polls latest; updates stats lazily

    // Wait helpers
    bool waitForDistinctFrames(uint64_t targetDistinctFrames, int timeout_ms);
    bool waitForProgress(uint64_t minAdvance, int timeout_ms); // wait until last_frame_id advances by minAdvance
    bool waitNoAdvance(uint64_t window_ms); // ensure no frame id change over window (stagnation)

    Stats stats() const;            // snapshot
    LatencyStats latencyStats() const; // compute derived stats (mean, p95, max)

    // Toggles
    void setEnableLatency(bool v) { enable_latency_ = v; }
    void setEnableChecksumVerify(bool v) { verify_checksum_ = v; }
    void setLatencyCap(size_t cap) { latency_cap_ = cap; if (latencies_ns_.size() > latency_cap_) latencies_ns_.resize(latency_cap_); }

private:
    // Internal helpers (control)
    void closeControl();
    void closeData();
    void updateFrameStats(const FrameView& fv, uint64_t latency_ns);

    std::shared_ptr<spdlog::logger> log_;
    // Control
    ControlConfig ctrl_cfg_{}; bool ctrl_connected_=false; int wfd_=-1; int rfd_=-1; std::string handshake_json_;
    // Data
    ShmDataConfig shm_cfg_{}; bool data_connected_=false; std::unique_ptr<caldera::backend::transport::IWorldFrameClient> data_client_;
    // Stats
    Stats stats_{};
    uint64_t last_seen_frame_id_ = std::numeric_limits<uint64_t>::max();
    uint64_t connect_time_ns_ = 0; // steady_clock at successful data connect
    bool verify_checksum_ = true;
    bool enable_latency_ = true;
    size_t latency_cap_ = 4096; // configurable
    std::vector<uint64_t> latencies_ns_; // unsorted samples
};
