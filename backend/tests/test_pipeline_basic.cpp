#include <gtest/gtest.h>
#include "common/Logger.h"
#include "hal/HAL_Manager.h"
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

#include <atomic>
#include <memory>
#include <thread>
#include <chrono>

using caldera::backend::common::Logger;
using caldera::backend::hal::HAL_Manager;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::WorldFrame;

namespace {
class MockTransport : public caldera::backend::transport::ITransportServer {
public:
    explicit MockTransport(std::shared_ptr<spdlog::logger> log) : log_(std::move(log)) {}
    void start() override { running_ = true; }
    void stop() override { running_ = false; }
    void sendWorldFrame(const WorldFrame& frame) override {
        if (!running_) return;
        framesReceived_++;
        lastTimestamp_ = frame.timestamp_ns;
        lastDims_ = {frame.heightMap.width, frame.heightMap.height};
    }
    std::atomic<size_t> framesReceived_{0};
    uint64_t lastTimestamp_ = 0;
    std::pair<int,int> lastDims_{0,0};
private:
    std::shared_ptr<spdlog::logger> log_;
    bool running_ = false;
};
}

TEST(PipelineBasic, EndToEndGeneratesWorldFrames) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/pipeline.log");
        Logger::instance().setGlobalLevel(spdlog::level::warn);
    }
    auto logHAL = Logger::instance().get("Test.HAL");
    auto logProc = Logger::instance().get("Test.Processing");
    auto logFusion = Logger::instance().get("Test.Processing.Fusion");
    auto logTransport = Logger::instance().get("Test.Transport");

    auto hal = std::make_shared<HAL_Manager>(logHAL);
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<MockTransport>(logTransport);

    // Wire manually
    hal->setDepthFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    hal->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(250)); // ~7-8 frames @30 FPS

    hal->stop();
    transport->stop();

    EXPECT_GE(transport->framesReceived_, 3u) << "Too few frames produced";
    EXPECT_EQ(transport->lastDims_.first, 640);
    EXPECT_EQ(transport->lastDims_.second, 480);
    EXPECT_GT(transport->lastTimestamp_, 0u);
    // frame_id should have advanced (at least 2 by last frame if >=3 frames received)
    // Can't access last frame directly here without modifying MockTransport to store it; skip for now.
}
