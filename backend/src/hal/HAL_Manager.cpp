#include "HAL_Manager.h"

#include <spdlog/logger.h>
#include <chrono>
#include <thread>
#include <random>

using namespace std::chrono_literals;

namespace caldera::backend::hal {

HAL_Manager::HAL_Manager(std::shared_ptr<spdlog::logger> mainLogger,
             std::shared_ptr<spdlog::logger> udpLogger)
    : logger_(std::move(mainLogger)), udp_logger_(std::move(udpLogger)) {}

HAL_Manager::~HAL_Manager() { stop(); }

void HAL_Manager::setDepthFrameCallback(RawDepthFrameCallback cb) { on_depth_frame_ = std::move(cb); }

void HAL_Manager::start() {
    if (is_running_.exchange(true)) return;
    logger_->info("[HAL] Started worker thread.");
    worker_thread_ = std::thread(&HAL_Manager::workerLoop, this);
}

void HAL_Manager::stop() {
    if (!is_running_.exchange(false)) return;
    if (worker_thread_.joinable()) worker_thread_.join();
    logger_->info("[HAL] Stopped worker thread.");
}

void HAL_Manager::workerLoop() {
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint16_t> depthDist(0, 1500); // arbitrary depth range

    uint64_t counter = 0;
    while (is_running_) {
        RawDepthFrame frame;
        frame.sensorId = "FakeKinect_1";
        frame.timestamp_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        frame.width = 640;
        frame.height = 480;
        frame.data.resize(static_cast<size_t>(frame.width) * frame.height);
        for (auto &px : frame.data) {
            px = depthDist(rng);
        }
        if (udp_logger_ && (counter % 90 == 0)) {
            udp_logger_->debug("[HAL] Heartbeat depth frame {}", counter);
        }
        if (on_depth_frame_) {
            on_depth_frame_(frame);
        }
        ++counter;
        std::this_thread::sleep_for(33ms); // ~30 FPS
    }
}

} // namespace caldera::backend::hal
