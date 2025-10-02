#include "SyntheticSensorDevice.h"
#include "common/Checksum.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <random>

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
    std::mt19937 rng;
    while (running_.load()) {
        // Pause gate (simple sleep loop to avoid busy spin). Keeps steady_clock origin fresh when resuming.
        while (running_.load() && paused_.load()) {
            std::this_thread::sleep_for(1ms);
        }
        if (!running_.load()) break;
        fillPattern(depth); // static spatial pattern
        RawDepthFrame raw;
        raw.sensorId = cfg_.sensorId;
        raw.width = cfg_.width;
        raw.height = cfg_.height;
        raw.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
        raw.data = depth; // copy (fine for tests)
        produced_frames_.fetch_add(1, std::memory_order_relaxed);
        bool drop = false;
        uint32_t dropN = fi_dropEveryN_.load(std::memory_order_relaxed);
        if (dropN > 0) {
            if ((frame_counter_ + 1) % dropN == 0) drop = true; // drop  Nth,2N-th,...
        }
        uint32_t jitterMax = fi_jitterMaxMs_.load(std::memory_order_relaxed);
        if (!drop && jitterMax > 0) {
            if (!fi_rng_init_.load(std::memory_order_acquire)) {
                rng.seed(fi_seed_.load());
                fi_rng_init_.store(true, std::memory_order_release);
            }
            std::uniform_int_distribution<uint32_t> dist(0, jitterMax);
            uint32_t extra = dist(rng);
            if (extra) std::this_thread::sleep_for(std::chrono::milliseconds(extra));
        }
        if (!drop) {
            if (callback_) {
                callback_(raw, RawColorFrame{});
                emitted_frames_.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            dropped_frames_.fetch_add(1, std::memory_order_relaxed);
            if (log_ && log_->should_log(spdlog::level::debug)) {
                log_->debug("(fault) dropped frame future_id={} dropEveryN={}", frame_counter_ + 1, dropN);
            }
        }
        ++frame_counter_;
        // Auto-pause after N frames if configured
        uint64_t limit = stop_after_.load();
        if (limit > 0 && frame_counter_ >= limit) {
            // Ensure we only auto-pause once by clearing stop_after_ once triggered
            uint64_t expected = limit;
            if (stop_after_.compare_exchange_strong(expected, 0ULL)) {
                if (!paused_.load()) {
                    paused_.store(true);
                    if (log_) log_->info("SyntheticSensorDevice auto-paused after {} frames (stop_after)", frame_counter_);
                }
            }
        }
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
        case Pattern::STRIPES: {
            // Horizontal bands 4 pixels tall alternating high/low to test directional filters later
            for (int y = 0; y < h; ++y) {
                bool on = (y / 4) & 1;
                uint16_t v = on ? 1800 : 600;
                for (int x = 0; x < w; ++x) {
                    buf[static_cast<size_t>(y)*w + x] = v;
                }
            }
            break;
        }
        case Pattern::RADIAL: {
            // Concentric circles: intensity decreases with distance from center (scaled to 0..2000)
            float cx = (w - 1) / 2.0f;
            float cy = (h - 1) / 2.0f;
            float maxDist = std::sqrt(cx*cx + cy*cy);
            if (maxDist < 1e-5f) maxDist = 1.0f;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float dx = x - cx;
                    float dy = y - cy;
                    float d = std::sqrt(dx*dx + dy*dy) / maxDist; // 0..1
                    float inv = 1.0f - d; // center highest
                    uint16_t v = static_cast<uint16_t>(std::round(inv * 2000.0f));
                    buf[static_cast<size_t>(y)*w + x] = v;
                }
            }
            break;
        }
    }
}

uint32_t SyntheticSensorDevice::computeCRC(const std::vector<uint16_t>& buf) const {
    return caldera::backend::common::crc32_bytes(reinterpret_cast<const uint8_t*>(buf.data()), buf.size() * sizeof(uint16_t));
}

void SyntheticSensorDevice::pause() {
    if (running_.load()) paused_.store(true);
}

void SyntheticSensorDevice::resume() {
    if (running_.load()) {
        paused_.store(false);
        if (log_) log_->info("SyntheticSensorDevice resumed id={}", cfg_.sensorId);
    }
}

void SyntheticSensorDevice::setStopAfter(uint64_t frames) {
    stop_after_.store(frames);
}

void SyntheticSensorDevice::configureFaultInjection(const FaultInjectionConfig& fic) {
    fi_dropEveryN_.store(fic.dropEveryN, std::memory_order_relaxed);
    fi_jitterMaxMs_.store(fic.jitterMaxMs, std::memory_order_relaxed);
    fi_seed_.store(fic.seed, std::memory_order_relaxed);
    fi_rng_init_.store(false, std::memory_order_relaxed);
    if (log_) {
        log_->info("Configured fault injection dropEveryN={} jitterMaxMs={} seed=0x{:X}", fic.dropEveryN, fic.jitterMaxMs, fic.seed);
    }
}

} // namespace caldera::backend::hal
