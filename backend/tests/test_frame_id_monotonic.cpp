#include <gtest/gtest.h>
#include <vector>
#include <chrono>
#include <thread>
#include "common/Logger.h"
#include "hal/ISensorDevice.h"
#include <random>
#include <chrono>
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

using caldera::backend::common::Logger;
using caldera::backend::hal::ISensorDevice;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::WorldFrame;

namespace {
class TestSyntheticDevice : public ISensorDevice {
public:
    bool open() override { if (running_) return true; running_=true; worker_=std::thread([this]{ run(); }); return true; }
    void close() override { running_=false; if(worker_.joinable()) worker_.join(); }
    bool isRunning() const override { return running_; }
    std::string getDeviceID() const override { return "FrameIdSynth"; }
    void setFrameCallback(caldera::backend::hal::RawFrameCallback callback) override { callback_=std::move(callback);} 
private:
    void run(){ std::mt19937 rng{std::random_device{}()}; std::uniform_int_distribution<uint16_t> dist(0,1500); while(running_){ if(callback_){ caldera::backend::common::RawDepthFrame d; d.sensorId="FrameIdSynth"; d.width=16; d.height=8; d.timestamp_ns= static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()); d.data.resize(d.width*d.height); for(auto &px:d.data) px=dist(rng); caldera::backend::common::RawColorFrame c; c.sensorId=d.sensorId; c.timestamp_ns=d.timestamp_ns; callback_(d,c);} std::this_thread::sleep_for(std::chrono::milliseconds(20)); }}
    std::atomic<bool> running_{false}; std::thread worker_; caldera::backend::hal::RawFrameCallback callback_;
};
class CaptureTransport : public caldera::backend::transport::ITransportServer {
public:
    explicit CaptureTransport(std::shared_ptr<spdlog::logger> l): log_(std::move(l)) {}
    void start() override { running_ = true; }
    void stop() override { running_ = false; }
    void sendWorldFrame(const WorldFrame& f) override {
        if(!running_) return;
        frames_.push_back(f.frame_id);
    }
    std::vector<uint64_t> frames_;
private:
    std::shared_ptr<spdlog::logger> log_;
    bool running_ = false;
};
}

TEST(FrameId, MonotonicIncreasing) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/frame_id.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logHAL = Logger::instance().get("Test.FrameId.HAL");
    auto logProc = Logger::instance().get("Test.FrameId.Proc");
    auto logFusion = Logger::instance().get("Test.FrameId.Proc.Fusion");
    auto logTransport = Logger::instance().get("Test.FrameId.Transport");

    auto device = std::make_unique<TestSyntheticDevice>();
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<CaptureTransport>(logTransport);

    device->setFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f, const caldera::backend::common::RawColorFrame&){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    device->open();
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    device->close();
    transport->stop();

    ASSERT_GE(transport->frames_.size(), 2u);
    for (size_t i=1;i<transport->frames_.size();++i) {
        EXPECT_EQ(transport->frames_[i], transport->frames_[i-1] + 1) << "Non-consecutive frame id";
    }
}
