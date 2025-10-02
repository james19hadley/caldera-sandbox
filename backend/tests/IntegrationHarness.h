// IntegrationHarness.h - Phase 0 test harness
#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <array>
#include <algorithm>

#include "hal/SyntheticSensorDevice.h"
#include "processing/ProcessingManager.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"
#include "common/Logger.h"

namespace caldera::backend::tests {

struct HarnessConfig {
    std::string shm_name = "/caldera_integration_phase0";
    int max_width = 64;
    int max_height = 64;
    float processing_scale = -1.0f; // if <0 use ProcessingManager default env-based resolution
};

class IntegrationHarness {
public:
    IntegrationHarness() = default;
    ~IntegrationHarness() { stop(); }

    void addSyntheticSensor(const caldera::backend::hal::SyntheticSensorDevice::Config& cfg) {
        sensors_.push_back(std::make_unique<caldera::backend::hal::SyntheticSensorDevice>(cfg, logger("Harness.Sensor")));
    }

    bool start(const HarnessConfig& hc) {
        if (running_) return false;
        harness_cfg_ = hc;
        // Transport
        caldera::backend::transport::SharedMemoryTransportServer::Config tcfg;
        tcfg.shm_name = harness_cfg_.shm_name;
        tcfg.max_width = harness_cfg_.max_width;
        tcfg.max_height = harness_cfg_.max_height;
        transport_ = std::make_unique<caldera::backend::transport::SharedMemoryTransportServer>(logger("Harness.Transport"), tcfg);
        transport_->start();

        // Processing
        processing_ = std::make_unique<caldera::backend::processing::ProcessingManager>(logger("Harness.Processing"), nullptr, harness_cfg_.processing_scale);
        processing_->setWorldFrameCallback([this](const caldera::backend::common::WorldFrame& wf){
            // Publish + latency accounting
            auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (wf.timestamp_ns <= now_ns) {
                uint64_t lat = now_ns - wf.timestamp_ns;
                last_latency_ns_.store(lat, std::memory_order_relaxed);
                latency_sum_ns_.fetch_add(lat, std::memory_order_relaxed);
                auto idx = latency_count_.fetch_add(1, std::memory_order_relaxed);
                if (idx < latency_samples_.size()) {
                    latency_samples_[static_cast<size_t>(idx)].store(lat, std::memory_order_relaxed);
                }
            }
            transport_->sendWorldFrame(wf);
            frames_published_.fetch_add(1, std::memory_order_relaxed);
            frames_out_.fetch_add(1, std::memory_order_relaxed);
        });

        // Sensors
        for (auto& s : sensors_) {
            s->setFrameCallback([this](const caldera::backend::common::RawDepthFrame& d, const caldera::backend::common::RawColorFrame&){
                frames_in_.fetch_add(1, std::memory_order_relaxed);
                processing_->processRawDepthFrame(d);
            });
            s->open();
        }
        running_ = true;
        return true;
    }

    void stop() {
        if (!running_) return;
        for (auto& s : sensors_) s->close();
        sensors_.clear();
        if (transport_) { transport_->stop(); transport_.reset(); }
        processing_.reset();
        // Unlink SHM segment so tests leave no residue
        if (!harness_cfg_.shm_name.empty()) shm_unlink(harness_cfg_.shm_name.c_str());
        running_ = false;
    }

    uint64_t framesPublished() const { return frames_published_.load(std::memory_order_relaxed); }
    struct Stats { uint64_t frames_in=0; uint64_t frames_out=0; uint64_t frames_published=0; uint64_t last_latency_ns=0; double mean_latency_ns=0.0; uint64_t derived_dropped=0; uint64_t p95_latency_ns=0; };
    Stats stats() const {
        Stats st;
        st.frames_in = frames_in_.load(std::memory_order_relaxed);
        st.frames_out = frames_out_.load(std::memory_order_relaxed);
        st.frames_published = frames_published_.load(std::memory_order_relaxed);
        st.last_latency_ns = last_latency_ns_.load(std::memory_order_relaxed);
        uint64_t lc = latency_count_.load(std::memory_order_relaxed);
        if (lc > 0) st.mean_latency_ns = static_cast<double>(latency_sum_ns_.load(std::memory_order_relaxed)) / static_cast<double>(lc);
        if (st.frames_in >= st.frames_out) st.derived_dropped = st.frames_in - st.frames_out; else st.derived_dropped = 0;
        st.p95_latency_ns = latencyP95ns();
        return st;
    }
    const HarnessConfig& cfg() const { return harness_cfg_; }
    // Test-only direct sensor access (returns nullptr if index out of range)
    caldera::backend::hal::SyntheticSensorDevice* syntheticSensor(size_t idx=0) {
        if (idx >= sensors_.size()) return nullptr;
        return sensors_[idx].get();
    }

private:
    uint64_t latencyP95ns() const {
        size_t count = std::min<size_t>(latency_count_.load(std::memory_order_relaxed), latency_samples_.size());
        if (count == 0) return 0;
        std::vector<uint64_t> copy; copy.reserve(count);
        for (size_t i=0;i<count;++i) copy.push_back(latency_samples_[i].load(std::memory_order_relaxed));
        std::sort(copy.begin(), copy.end());
        size_t idx = static_cast<size_t>(std::ceil(0.95 * copy.size())) - 1; if (idx >= copy.size()) idx = copy.size()-1;
        return copy[idx];
    }

    std::shared_ptr<spdlog::logger> logger(const std::string& name) {
        auto& L = caldera::backend::common::Logger::instance();
        if (!L.isInitialized()) L.initialize("logs/test/integration_phase0.log");
        return L.get(name);
    }

    HarnessConfig harness_cfg_{};
    std::vector<std::unique_ptr<caldera::backend::hal::SyntheticSensorDevice>> sensors_;
    std::unique_ptr<caldera::backend::processing::ProcessingManager> processing_;
    std::unique_ptr<caldera::backend::transport::SharedMemoryTransportServer> transport_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_published_{0};
    std::atomic<uint64_t> frames_in_{0};
    std::atomic<uint64_t> frames_out_{0};
    std::atomic<uint64_t> last_latency_ns_{0};
    std::atomic<uint64_t> latency_sum_ns_{0};
    std::atomic<uint64_t> latency_count_{0};
    std::array<std::atomic<uint64_t>,512> latency_samples_{}; // first 512 samples for p95
};

} // namespace caldera::backend::tests
