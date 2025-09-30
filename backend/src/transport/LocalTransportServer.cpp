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

void LocalTransportServer::start() {
    if (running_.exchange(true)) return;
    logger_->info("LocalTransportServer starting (mock mode)");
    if (handshake_logger_) handshake_logger_->info("Handshake sequence simulated");
}

void LocalTransportServer::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    logger_->info("LocalTransportServer stopped");
}

void LocalTransportServer::sendWorldFrame(const data::WorldFrame& frame) {
    if (!running_) return;
    if (logger_->should_log(spdlog::level::debug)) {
        logger_->debug("Send frame {} ts={} info='{}'", frame.frameIndex, frame.timestamp_ns, frame.debugInfo);
    }
}

} // namespace caldera::backend::transport
