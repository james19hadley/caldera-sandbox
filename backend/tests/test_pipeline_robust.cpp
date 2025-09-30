#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

#include "common/Logger.h"
#include "hal/HAL_Manager.h"
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

using namespace std::chrono_literals;

using caldera::backend::common::Logger;
using caldera::backend::hal::HAL_Manager;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::WorldFrame;

namespace {
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

    auto hal = std::make_shared<HAL_Manager>(logHAL);
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<CountingTransport>(logTransport);
    hal->setDepthFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    for (int i=0;i<5;++i) {
        hal->start();
        std::this_thread::sleep_for(50ms);
        hal->stop();
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

    auto hal = std::make_shared<HAL_Manager>(logHAL);
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<CountingTransport>(logTransport);
    hal->setDepthFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    hal->start();
    std::this_thread::sleep_for(330ms); // ~10 frames expected at 30 FPS
    hal->stop();
    transport->stop();

    size_t produced = transport->count_.load();
    // Allow a broad window [5, 14] to be robust across CI noise
    EXPECT_GE(produced, 5u);
    EXPECT_LE(produced, 14u);
}
