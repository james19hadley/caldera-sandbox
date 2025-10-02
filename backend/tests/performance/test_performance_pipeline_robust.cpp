#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

#include "common/Logger.h"
#include "hal/ISensorDevice.h"
#include <random>
#include <chrono>
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

using namespace std::chrono_literals;

using caldera::backend::common::Logger;
using caldera::backend::hal::ISensorDevice;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::WorldFrame;

namespace {
class TestSyntheticDevice : public ISensorDevice {
public:
    bool open() override {
        if (running_) return true;
        running_ = true;
        worker_ = std::thread([this]{
            std::mt19937 rng{std::random_device{}()};
            std::uniform_int_distribution<uint16_t> dist(0,1500);
            while (running_) {
                if (callback_) {
                    caldera::backend::common::RawDepthFrame d; d.sensorId="RobustSynth"; d.width=64; d.height=48; d.timestamp_ns= static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
                    d.data.resize(d.width*d.height);
                    for (auto &px: d.data) px=dist(rng);
                    caldera::backend::common::RawColorFrame c; c.sensorId=d.sensorId; c.timestamp_ns=d.timestamp_ns;
                    callback_(d,c);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        });
        return true;
    }
    void close() override {
        if (!running_) return; running_=false; if(worker_.joinable()) worker_.join();
    }
    bool isRunning() const override { return running_; }
    std::string getDeviceID() const override { return "RobustSynth"; }
    void setFrameCallback(caldera::backend::hal::RawFrameCallback callback) override { callback_ = std::move(callback); }
private:
    std::atomic<bool> running_{false};
    std::thread worker_;
    caldera::backend::hal::RawFrameCallback callback_;
};
class CountingTransport : public caldera::backend::transport::ITransportServer {
public:
    explicit CountingTransport(std::shared_ptr<spdlog::logger> log): log_(std::move(log)) {}
    void start() override { running_ = true; }
    void stop() override { running_ = false; }
    void sendWorldFrame(const WorldFrame& f) override {
        if(!running_) return;
        count_++;
        last_id_ = f.frame_id;
    }
    std::atomic<size_t> count_{0};
    std::atomic<uint64_t> last_id_{0};
private:
    std::shared_ptr<spdlog::logger> log_;
    bool running_ = false;
};
}

TEST(PipelineRobust, RapidStartStopHAL) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/pipeline_robust.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logHAL = Logger::instance().get("Test.Robust.HAL");
    auto logProc = Logger::instance().get("Test.Robust.Proc");
    auto logFusion = Logger::instance().get("Test.Robust.Proc.Fusion");
    auto logTransport = Logger::instance().get("Test.Robust.Transport");

    auto device = std::make_unique<TestSyntheticDevice>();
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<CountingTransport>(logTransport);
    device->setFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f, const caldera::backend::common::RawColorFrame&){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    for (int i=0;i<5;++i) {
        device->open();
        std::this_thread::sleep_for(50ms);
        device->close();
    }
    transport->stop();

    // We expect at least a few frames overall despite rapid cycling.
    EXPECT_GE(transport->count_.load(), 2u);
}

TEST(PipelineRobust, FrameRateApproximation) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/pipeline_robust.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logHAL = Logger::instance().get("Test.Robust.HAL2");
    auto logProc = Logger::instance().get("Test.Robust.Proc2");
    auto logFusion = Logger::instance().get("Test.Robust.Proc2.Fusion");
    auto logTransport = Logger::instance().get("Test.Robust.Transport2");

    auto device2 = std::make_unique<TestSyntheticDevice>();
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<CountingTransport>(logTransport);
    device2->setFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f, const caldera::backend::common::RawColorFrame&){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    device2->open();
    std::this_thread::sleep_for(330ms); // ~10 frames expected at 30 FPS
    device2->close();
    transport->stop();

    size_t produced = transport->count_.load();
    // Allow a broad window [5, 14] to be robust across CI noise
    EXPECT_GE(produced, 5u);
    EXPECT_LE(produced, 14u);
}
