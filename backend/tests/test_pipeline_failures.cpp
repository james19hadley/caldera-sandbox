// Tests for pipeline/AppManager failure handling when sensor open fails.
#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>

#include "common/Logger.h"
#include "hal/ISensorDevice.h"
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"
#include "AppManager.h"

using caldera::backend::common::Logger;
using caldera::backend::hal::ISensorDevice;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::transport::ITransportServer;
using caldera::backend::AppManager;

namespace {
// A sensor device whose open() always fails.
class FailingSensorDevice : public ISensorDevice {
public:
    explicit FailingSensorDevice(std::shared_ptr<spdlog::logger> log): log_(std::move(log)) {}
    bool open() override {
        log_->error("FailingSensorDevice open() invoked - simulating failure");
        return false;
    }
    void close() override { /* no-op */ }
    bool isRunning() const override { return false; }
    std::string getDeviceID() const override { return "FailingSensor"; }
    void setFrameCallback(caldera::backend::hal::RawFrameCallback cb) override { callback_ = std::move(cb); }
private:
    std::shared_ptr<spdlog::logger> log_;
    caldera::backend::hal::RawFrameCallback callback_;
};

class CountingTransport : public ITransportServer {
public:
    explicit CountingTransport(std::shared_ptr<spdlog::logger> log, std::atomic<int>& counter)
        : log_(std::move(log)), counter_(counter) {}
    void start() override { running_ = true; }
    void stop() override { running_ = false; }
    void sendWorldFrame(const caldera::backend::common::WorldFrame&) override {
        if (!running_) return;
        counter_++; // Should never be hit in this test
    }
private:
    std::shared_ptr<spdlog::logger> log_;
    bool running_ = false;
    std::atomic<int>& counter_;
};
}

TEST(PipelineFailures, SensorOpenFailureNoFrames) {
    // Ensure logger
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/pipeline_failures.log", spdlog::level::err);
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logLifecycle = Logger::instance().get("Test.Pipeline.Fail.Lifecycle");
    auto logSensor = Logger::instance().get("Test.Pipeline.Fail.Sensor");
    auto logProc = Logger::instance().get("Test.Pipeline.Fail.Proc");
    auto logFusion = Logger::instance().get("Test.Pipeline.Fail.Proc.Fusion");
    auto logTransport = Logger::instance().get("Test.Pipeline.Fail.Transport");

    // Counting transport increments only if a WorldFrame is delivered.
    std::atomic<int> frameCount{0};
    auto transport = std::make_shared<CountingTransport>(logTransport, frameCount);

    // Processing manager with a callback counting frames (via transport) - scale sentinel (-1.0f) ok.
    auto processing = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);

    // Build AppManager with failing device.
    auto failingDevice = std::make_unique<FailingSensorDevice>(logSensor);
    AppManager app(logLifecycle, std::move(failingDevice), processing, transport);

    // Start app: open should fail but not crash. Transport still starts as per current implementation.
    EXPECT_NO_FATAL_FAILURE(app.start());

    // Allow brief time in case of unintended asynchronous callbacks.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Assert no frames processed.
    EXPECT_EQ(frameCount.load(), 0) << "No frames should be produced when sensor open fails";

    // Stop should be safe and idempotent.
    EXPECT_NO_FATAL_FAILURE(app.stop());
}
