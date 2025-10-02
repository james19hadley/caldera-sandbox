#include "LocalTransportServer.h"

#include <spdlog/logger.h>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std::chrono_literals;

namespace caldera::backend::transport {

LocalTransportServer::LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                                           std::shared_ptr<spdlog::logger> handshakeLogger)
    : logger_(std::move(logger)), handshake_logger_(std::move(handshakeLogger)) {}

LocalTransportServer::LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                                           std::shared_ptr<spdlog::logger> handshakeLogger,
                                           Config cfg)
    : logger_(std::move(logger)), handshake_logger_(std::move(handshakeLogger)), cfg_(std::move(cfg)) {}

LocalTransportServer::~LocalTransportServer() { stop(); }

static constexpr const char* CLIENT_HELLO = "HELLO_CALDERA_CLIENT_V1";

void LocalTransportServer::start() {
    if (running_.exchange(true)) return;
    // Reset state for (potential) restart reuse
    handshake_completed_.store(false);
    last_heartbeat_ns_.store(0);
    last_log_heartbeat_ns_.store(0);
    last_stats_emit_ns_.store(0);
    if (wfd_s2c_ >= 0) { ::close(wfd_s2c_); wfd_s2c_ = -1; }
    logger_->info("LocalTransportServer starting (async handshake)");
    worker_ = std::thread(&LocalTransportServer::workerLoop, this);
}

void LocalTransportServer::stop() {
    // Ensure running_ is false, but always join thread if joinable to avoid std::terminate.
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
        logger_->info("LocalTransportServer stopped");
    }
}

void LocalTransportServer::sendWorldFrame(const caldera::backend::common::WorldFrame& frame) {
    if (!running_ || !handshake_completed_.load()) return; // only after successful handshake
    if (logger_->should_log(spdlog::level::debug)) {
        const auto& hm = frame.heightMap;
    logger_->debug("Send WorldFrame id={} ts={} map={}x{}", frame.frame_id, frame.timestamp_ns, hm.width, hm.height);
    }
}

bool LocalTransportServer::isClientAlive(std::chrono::milliseconds timeout) const {
    uint64_t last = last_heartbeat_ns_.load();
    if (last == 0) return false;
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (now_ns - last) <= static_cast<uint64_t>(timeout.count()) * 1'000'000ULL;
}

void LocalTransportServer::removeFifos() {
    ::unlink(cfg_.pipe_s2c.c_str());
    ::unlink(cfg_.pipe_c2s.c_str());
}

void LocalTransportServer::workerLoop() {
    // Generate SHM names early
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    shm_name_a_ = "/caldera_shm_a_" + std::to_string(ms);
    shm_name_b_ = "/caldera_shm_b_" + std::to_string(ms);
    shm_size_ = 1024 * 1024;

    // Create FIFOs (overwrite old if present)
    fifo_s2c_.create(cfg_.pipe_s2c, true);
    fifo_c2s_.create(cfg_.pipe_c2s, true);
    if (handshake_logger_) handshake_logger_->info("Handshake: FIFOs created s2c='{}' c2s='{}'", cfg_.pipe_s2c, cfg_.pipe_c2s);

    // Handshake phase: wait for HELLO on c2s then respond JSON on s2c
    int rfd = fifo_c2s_.openForReading(false); // non-block for poll loop
    if (rfd < 0) { logger_->error("Handshake: failed open c2s for read"); running_.store(false); removeFifos(); return; }
    std::string hello; char ch; int waited = 0;
    if (handshake_logger_) handshake_logger_->info("Handshake: waiting for client HELLO (timeout ms={})", cfg_.handshake_timeout_ms);
    while (running_ && waited < cfg_.handshake_timeout_ms) {
        ssize_t n = ::read(rfd, &ch, 1);
        if (n == 0) { std::this_thread::sleep_for(10ms); waited+=10; continue; }
        if (n < 0) { if (errno==EAGAIN || errno==EWOULDBLOCK) { std::this_thread::sleep_for(10ms); waited+=10; continue; } break; }
        if (ch == '\n') break; else hello.push_back(ch);
        if (hello.size() > 256) break;
        if (handshake_logger_ && (hello.size() % 8) == 0) handshake_logger_->info("Handshake: partial HELLO bytes='{}'", hello);
    }
    fifo_c2s_.closePipe(rfd);
    if (hello != CLIENT_HELLO) {
        if (handshake_logger_) handshake_logger_->error("Handshake failed (got='{}')", hello);
        running_.store(false); removeFifos(); return;
    }
    if (handshake_logger_) handshake_logger_->info("Handshake: received complete HELLO ({} bytes)", hello.size());
    // Respond
    wfd_s2c_ = fifo_s2c_.openForWriting(true);
    if (wfd_s2c_ < 0) { logger_->error("Handshake: open s2c write failed"); running_.store(false); removeFifos(); return; }
    std::string json = std::string("{\n") +
        "  \"protocol_version\": \"1.0\",\n" +
        "  \"shm_name_a\": \"" + shm_name_a_ + "\",\n" +
        "  \"shm_name_b\": \"" + shm_name_b_ + "\",\n" +
        "  \"shm_size\": " + std::to_string(shm_size_) + ",\n" +
        "  \"height_map_width\": 512,\n" +
        "  \"height_map_height\": 512\n" +
        "}";
    fifo_s2c_.writeLine(wfd_s2c_, json);
    handshake_completed_.store(true);
    if (handshake_logger_) handshake_logger_->info("Handshake complete (dual FIFO)");

    // Open c2s again for continuous message stream
    int cfd = fifo_c2s_.openForReading(false);
    if (cfd < 0) { logger_->error("Failed reopen c2s for heartbeat"); running_.store(false); removeFifos(); return; }
    if (handshake_logger_) handshake_logger_->info("Heartbeat loop started");

    // Heartbeat & stats loop
    char buf[256]; std::string line;
    while (running_) {
        ssize_t n = ::read(cfd, buf, sizeof(buf));
        if (n > 0) {
            for (ssize_t i=0;i<n;++i) {
                char c = buf[i];
                if (c == '\n') {
                    if (!line.empty()) {
                        if (line.find("heartbeat") != std::string::npos) {
                            last_heartbeat_ns_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count());
                            uint64_t last_log = last_log_heartbeat_ns_.load();
                            uint64_t now_ns2 = last_heartbeat_ns_.load();
                            if (now_ns2 - last_log > static_cast<uint64_t>(cfg_.heartbeat_log_throttle_ms) * 1'000'000ULL) {
                                last_log_heartbeat_ns_.store(now_ns2);
                                if (handshake_logger_) handshake_logger_->info("Heartbeat OK");
                            }
                        } else if (line.find("telemetry") != std::string::npos) {
                            if (handshake_logger_) handshake_logger_->info("Telemetry: {}", line);
                            last_heartbeat_ns_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count());
                        } else {
                            if (handshake_logger_) handshake_logger_->warn("Unknown client msg: {}", line);
                        }
                    }
                    line.clear();
                } else {
                    if (line.size() < static_cast<size_t>(cfg_.max_json_field_len)) line.push_back(c);
                }
            }
        } else if (n < 0) {
            if (!(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) {
                break; // real error
            }
        } else { // n == 0 -> no writers or no data yet; short sleep
            std::this_thread::sleep_for(20ms);
        }

        // Periodic server stats emission independent of inbound data volume
        if (cfg_.server_stats_interval_ms > 0 && wfd_s2c_ >= 0) {
            uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            uint64_t interval_ns = static_cast<uint64_t>(cfg_.server_stats_interval_ms) * 1'000'000ULL;
            uint64_t last = last_stats_emit_ns_.load();
            if (last == 0 || now_ns - last >= interval_ns) {
                last_stats_emit_ns_.store(now_ns);
                if (stats_json_provider_) {
                    std::string stats_json = stats_json_provider_();
                    if (!stats_json.empty()) {
                        if (stats_json.back() != '\n') stats_json.push_back('\n');
                        ssize_t wn = ::write(wfd_s2c_, stats_json.c_str(), stats_json.size());
                        if (wn < 0 && errno == EPIPE) {
                            if (handshake_logger_) handshake_logger_->warn("Stats pipe closed by client (EPIPE), stopping stats emission");
                            ::close(wfd_s2c_); wfd_s2c_ = -1;
                        } else if (handshake_logger_) {
                            handshake_logger_->info("ServerStats sent: {}", stats_json.substr(0, stats_json.size()-1));
                        }
                    }
                }
            }
        }

        std::this_thread::sleep_for(5ms); // modest loop pacing
    }
    if (handshake_logger_) handshake_logger_->info("Worker loop exiting (running_={} handshake_complete={})", running_.load(), handshake_completed_.load());
    fifo_c2s_.closePipe(cfd);
    if (wfd_s2c_ >= 0) { ::close(wfd_s2c_); wfd_s2c_ = -1; }
    removeFifos();
}

} // namespace caldera::backend::transport
