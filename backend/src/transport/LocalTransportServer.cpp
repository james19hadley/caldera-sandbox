#include "LocalTransportServer.h"

#include <spdlog/logger.h>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace caldera::backend::transport {

LocalTransportServer::LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                                           std::shared_ptr<spdlog::logger> handshakeLogger)
    : logger_(std::move(logger)), handshake_logger_(std::move(handshakeLogger)) {}

LocalTransportServer::~LocalTransportServer() { stop(); }

static constexpr const char* HANDSHAKE_PIPE_PATH = "/tmp/caldera_handshake";
static constexpr const char* CLIENT_HELLO = "HELLO_CALDERA_CLIENT_V1";

void LocalTransportServer::start() {
    if (running_.exchange(true)) return;
    logger_->info("LocalTransportServer starting");
    if (handshake_logger_) handshake_logger_->info("Setting up handshake FIFO at {}", HANDSHAKE_PIPE_PATH);

    // Prepare shared memory names (placeholder deterministic or randomizable ids)
    // For now generate simple timestamp-based suffix
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    shm_name_a_ = "/caldera_shm_a_" + std::to_string(ms);
    shm_name_b_ = "/caldera_shm_b_" + std::to_string(ms);
    shm_size_ = 1024 * 1024; // 1 MB placeholder size

    // Create FIFO
    fifo_ = FifoManager(handshake_logger_);
    if (!fifo_.create(HANDSHAKE_PIPE_PATH, true)) {
        if (handshake_logger_) handshake_logger_->error("Failed to create handshake FIFO; aborting start");
        running_.store(false);
        return;
    }

    // Open for reading (blocks until client connects for writing)
    if (handshake_logger_) handshake_logger_->info("Waiting for client to connect (open read on FIFO will block)...");
    int rfd = fifo_.openForReading(true);
    if (rfd < 0) {
        running_.store(false);
        return;
    }
    std::string hello = fifo_.readLine(rfd);
    fifo_.closePipe(rfd);
    if (handshake_logger_) handshake_logger_->info("Received handshake line='{}'", hello);
    if (hello != CLIENT_HELLO) {
        if (handshake_logger_) handshake_logger_->error("Invalid client HELLO; expected {}", CLIENT_HELLO);
        running_.store(false);
        fifo_.remove();
        return;
    }

    // Re-open for writing to send JSON config
    int wfd = fifo_.openForWriting(true);
    if (wfd < 0) {
        running_.store(false);
        fifo_.remove();
        return;
    }
    // Build JSON (manual serialization for simplicity)
    std::string json = std::string("{\n") +
        "  \"protocol_version\": \"1.0\",\n" +
        "  \"shm_name_a\": \"" + shm_name_a_ + "\",\n" +
        "  \"shm_name_b\": \"" + shm_name_b_ + "\",\n" +
        "  \"shm_size\": " + std::to_string(shm_size_) + ",\n" +
        "  \"height_map_width\": 512,\n" +
        "  \"height_map_height\": 512\n" +
        "}";
    fifo_.writeLine(wfd, json);
    fifo_.closePipe(wfd);
    if (handshake_logger_) handshake_logger_->info("Handshake complete; provided shm names: {}, {}", shm_name_a_, shm_name_b_);

    // Remove FIFO after handshake (one-shot). If persistent desired, skip this.
    fifo_.remove();
}

void LocalTransportServer::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    logger_->info("LocalTransportServer stopped");
}

void LocalTransportServer::sendWorldFrame(const caldera::backend::common::WorldFrame& frame) {
    if (!running_) return;
    if (logger_->should_log(spdlog::level::debug)) {
        const auto& hm = frame.heightMap;
    logger_->debug("Send WorldFrame id={} ts={} map={}x{}", frame.frame_id, frame.timestamp_ns, hm.width, hm.height);
    }
}

} // namespace caldera::backend::transport
