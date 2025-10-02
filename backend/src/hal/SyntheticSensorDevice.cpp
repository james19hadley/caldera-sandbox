#include "SyntheticSensorDevice.h"
#include "common/Checksum.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using namespace std::chrono_literals;
namespace caldera::backend::hal {

SyntheticSensorDevice::SyntheticSensorDevice(const Config& cfg, std::shared_ptr<spdlog::logger> log)
    : cfg_(cfg), log_(std::move(log)) {}

bool SyntheticSensorDevice::open() {
    if (running_.load()) return true;
    running_.store(true);
    // Precompute base pattern checksum once.
    std::vector<uint16_t> tmp(static_cast<size_t>(cfg_.width) * cfg_.height);
    fillPattern(tmp);
    base_checksum_ = computeCRC(tmp);
    worker_ = std::thread(&SyntheticSensorDevice::runLoop, this);
    if (log_) log_->info("SyntheticSensorDevice started id={} size={}x{} fps={}", cfg_.sensorId, cfg_.width, cfg_.height, cfg_.fps);
    return true;
}

void SyntheticSensorDevice::close() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    if (log_) log_->info("SyntheticSensorDevice stopped id={}", cfg_.sensorId);
}

void SyntheticSensorDevice::runLoop() {
    using clock = std::chrono::steady_clock;
    auto period = std::chrono::duration<double>(1.0 / cfg_.fps);
    auto next_tp = clock::now();
    std::vector<uint16_t> depth(static_cast<size_t>(cfg_.width) * cfg_.height);
    while (running_.load()) {
        fillPattern(depth); // static spatial pattern
        RawDepthFrame raw;
        raw.sensorId = cfg_.sensorId;
        raw.width = cfg_.width;
        raw.height = cfg_.height;
        raw.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
        raw.data = depth; // copy (fine for tests)
        if (callback_) callback_(raw, RawColorFrame{}); // color empty
        ++frame_counter_;
        next_tp += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        std::this_thread::sleep_until(next_tp);
    }
}

void SyntheticSensorDevice::fillPattern(std::vector<uint16_t>& buf) const {
    const int w = cfg_.width;
    const int h = cfg_.height;
    switch (cfg_.pattern) {
        case Pattern::RAMP: {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    buf[static_cast<size_t>(y)*w + x] = static_cast<uint16_t>(x + y);
                }
            }
            break;
        }
        case Pattern::CONSTANT: {
            std::fill(buf.begin(), buf.end(), cfg_.constantValue);
            break;
        }
        case Pattern::CHECKER: {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    bool on = ((x/2) + (y/2)) & 1; // 2x2 block checker
                    buf[static_cast<size_t>(y)*w + x] = on ? 2000 : 500;
                }
            }
            break;
        }
    }
}

uint32_t SyntheticSensorDevice::computeCRC(const std::vector<uint16_t>& buf) const {
    return caldera::backend::common::crc32_bytes(reinterpret_cast<const uint8_t*>(buf.data()), buf.size() * sizeof(uint16_t));
}

} // namespace caldera::backend::hal
