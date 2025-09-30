#include <gtest/gtest.h>
#include <vector>
#include <chrono>
#include <thread>
#include "common/Logger.h"
#include "hal/HAL_Manager.h"
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

using caldera::backend::common::Logger;
using caldera::backend::hal::HAL_Manager;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::WorldFrame;

namespace {
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

    auto hal = std::make_shared<HAL_Manager>(logHAL);
    auto proc = std::make_shared<ProcessingManager>(logProc, logFusion, -1.0f);
    auto transport = std::make_shared<CaptureTransport>(logTransport);

    hal->setDepthFrameCallback([proc](const caldera::backend::common::RawDepthFrame& f){ proc->processRawDepthFrame(f); });
    proc->setWorldFrameCallback([transport](const WorldFrame& wf){ transport->sendWorldFrame(wf); });

    transport->start();
    hal->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    hal->stop();
    transport->stop();

    ASSERT_GE(transport->frames_.size(), 2u);
    for (size_t i=1;i<transport->frames_.size();++i) {
        EXPECT_EQ(transport->frames_[i], transport->frames_[i-1] + 1) << "Non-consecutive frame id";
    }
}
