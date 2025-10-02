// IntegrationHarness.h - Phase 0 test harness
#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <thread>

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
        processing_ = std::make_unique<caldera::backend::processing::ProcessingManager>(logger("Harness.Processing"));
        processing_->setWorldFrameCallback([this](const caldera::backend::common::WorldFrame& wf){
            transport_->sendWorldFrame(wf);
            frames_published_.fetch_add(1, std::memory_order_relaxed);
        });

        // Sensors
        for (auto& s : sensors_) {
            s->setFrameCallback([this](const caldera::backend::common::RawDepthFrame& d, const caldera::backend::common::RawColorFrame&){
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
    const HarnessConfig& cfg() const { return harness_cfg_; }

private:
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
};

} // namespace caldera::backend::tests
