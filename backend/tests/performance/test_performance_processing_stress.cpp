#include <gtest/gtest.h>
#include "common/Logger.h"
#include "processing/ProcessingManager.h"
#include <vector>

using caldera::backend::common::Logger;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;

TEST(ProcessingStress, Burst1000Frames) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/processing_stress.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logProc = Logger::instance().get("Test.Stress.Proc");
    ProcessingManager pm(logProc, nullptr, -1.0f);
    size_t received = 0;
    uint64_t last_id = 0;
    pm.setWorldFrameCallback([&](const WorldFrame& wf){
        received++; last_id = wf.frame_id; });
    RawDepthFrame raw; raw.sensorId = "Burst"; raw.width = 32; raw.height = 32; raw.data.resize(32*32, 1000);
    for (int i=0;i<1000;++i) {
        raw.timestamp_ns = i;
        pm.processRawDepthFrame(raw);
    }
    EXPECT_EQ(received, 1000u);
    EXPECT_EQ(last_id, 999u);
}

TEST(ProcessingBasic, NoCallbackSafe) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/processing_stress.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logProc = Logger::instance().get("Test.Stress.Proc2");
    ProcessingManager pm(logProc, nullptr, -1.0f);
    RawDepthFrame raw; raw.sensorId = "NoCB"; raw.width = 4; raw.height = 4; raw.data.assign(16, 42);
    // Should not crash
    pm.processRawDepthFrame(raw);
    SUCCEED();
}
