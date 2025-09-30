#include "HAL_Manager.h"

#include <spdlog/logger.h>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace caldera::backend::hal {

HAL_Manager::HAL_Manager(std::shared_ptr<spdlog::logger> mainLogger,
                         std::shared_ptr<spdlog::logger> udpLogger)
    : logger_(std::move(mainLogger)), udp_logger_(std::move(udpLogger)) {}

HAL_Manager::~HAL_Manager() { stop(); }

void HAL_Manager::setRawDataCallback(RawDataCallback cb) { callback_ = std::move(cb); }

void HAL_Manager::start() {
    if (running_.exchange(true)) return;
    logger_->info("HAL_Manager starting (mock mode)");
    worker_ = std::thread(&HAL_Manager::runMockLoop, this);
}

void HAL_Manager::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    logger_->info("HAL_Manager stopped");
}

void HAL_Manager::runMockLoop() {
    uint64_t counter = 0;
    while (running_) {
        data::RawDataPacket pkt;
        pkt.timestamp_ns = counter * 16'666'666ull; // ~60Hz fake
        pkt.sourceId = 1;
        pkt.payload.assign(32, static_cast<uint8_t>(counter & 0xFF));
        if (udp_logger_) {
            if ((counter % 120) == 0) udp_logger_->debug("UDP heartbeat simulated");
        }
        if (callback_) callback_(pkt);
        ++counter;
        std::this_thread::sleep_for(16ms);
    }
}

} // namespace caldera::backend::hal
