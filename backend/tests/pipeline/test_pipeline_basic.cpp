#include <gtest/gtest.h>
#include "common/Logger.h"
#include "hal/ISensorDevice.h"
#include <atomic>
#include <thread>
#include <random>
#include <chrono>
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

#include <atomic>
#include <memory>
#include <thread>
#include <chrono>

using caldera::backend::common::Logger;
using caldera::backend::hal::ISensorDevice;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::WorldFrame;

namespace {
class TestSyntheticDevice : public ISensorDevice {
public:
    TestSyntheticDevice() = default;
    bool open() override {
        running_.store(true);
        worker_ = std::thread([this]{
            std::mt19937 rng{std::random_device{}()};
            std::uniform_int_distribution<uint16_t> dist(0,1500);
            while (running_) {
                if (callback_) {
                    caldera::backend::common::RawDepthFrame d; d.sensorId="TestSynth"; d.width=640; d.height=480; d.timestamp_ns = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
                    d.data.resize(640*480);
                    for (auto &px: d.data) px = dist(rng);
                    caldera::backend::common::RawColorFrame c; c.sensorId=d.sensorId; c.timestamp_ns=d.timestamp_ns;
                    callback_(d,c);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        });
        return true;
    }
    void close() override {
        running_.store(false);
        if (worker_.joinable()) worker_.join();
    }
    bool isRunning() const override { return running_.load(); }
    std::string getDeviceID() const override { return "TestSyntheticDevice"; }
    void setFrameCallback(caldera::backend::hal::RawFrameCallback callback) override { callback_ = std::move(callback); }
private:
    std::atomic<bool> running_{false};
    std::thread worker_;
    caldera::backend::hal::RawFrameCallback callback_;
};
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

    auto device = std::make_unique<TestSyntheticDevice>();
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<MockTransport>(logTransport);

    // Wire manually via device callback
    device->setFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f, const caldera::backend::common::RawColorFrame&){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    ASSERT_TRUE(device->open());

    std::this_thread::sleep_for(std::chrono::milliseconds(250)); // ~7-8 frames @30 FPS

    device->close();
    transport->stop();

    EXPECT_GE(transport->framesReceived_, 3u) << "Too few frames produced";
    EXPECT_EQ(transport->lastDims_.first, 640);
    EXPECT_EQ(transport->lastDims_.second, 480);
    EXPECT_GT(transport->lastTimestamp_, 0u);
    // frame_id should have advanced (at least 2 by last frame if >=3 frames received)
    // Can't access last frame directly here without modifying MockTransport to store it; skip for now.
}
