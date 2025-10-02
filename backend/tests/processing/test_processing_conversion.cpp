#include <gtest/gtest.h>
#include "common/Logger.h"
#include "processing/ProcessingManager.h"

using caldera::backend::common::Logger;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;

namespace {
struct CaptureCtx {
    WorldFrame frame; 
    bool received = false;
};
}

TEST(ProcessingConversion, DepthToHeightAndResize) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/processing.log");
        Logger::instance().setGlobalLevel(spdlog::level::warn);
    }
    auto procLogger = Logger::instance().get("Test.Processing");
    auto fusionLogger = Logger::instance().get("Test.Processing.Fusion");

    ProcessingManager pm(procLogger, fusionLogger, -1.0f);
    CaptureCtx ctx;
    pm.setWorldFrameCallback([&](const WorldFrame& wf){ ctx.frame = wf; ctx.received = true; });

    RawDepthFrame raw{};
    raw.sensorId = "TestSensor";
    raw.width = 4;
    raw.height = 2;
    raw.timestamp_ns = 123456789ULL;
    // Depth values chosen to exercise min / max / mid scaling; scale = 0.001
    raw.data = {0, 500, 1000, 1500, 250, 750, 1250, 1499};

    pm.processRawDepthFrame(raw);
    ASSERT_TRUE(ctx.received);
    EXPECT_EQ(ctx.frame.timestamp_ns, raw.timestamp_ns);
    EXPECT_EQ(ctx.frame.frame_id, 0u);
    ASSERT_EQ(ctx.frame.heightMap.width, 4);
    ASSERT_EQ(ctx.frame.heightMap.height, 2);
    ASSERT_EQ(ctx.frame.heightMap.data.size(), raw.data.size());

    // Verify scaling
    constexpr float scale = 1.0f/1000.0f;
    for (size_t i = 0; i < raw.data.size(); ++i) {
        EXPECT_FLOAT_EQ(ctx.frame.heightMap.data[i], static_cast<float>(raw.data[i]) * scale) << "Mismatch at index " << i;
    }

    // Compute stats from produced data
    float minV = ctx.frame.heightMap.data[0];
    float maxV = ctx.frame.heightMap.data[0];
    double sum = 0.0;
    for (float v : ctx.frame.heightMap.data) { minV = std::min(minV, v); maxV = std::max(maxV, v); sum += v; }
    float avg = static_cast<float>(sum / ctx.frame.heightMap.data.size());

    EXPECT_FLOAT_EQ(minV, 0.0f);
    EXPECT_NEAR(maxV, 1.5f, 0.0001f);
    EXPECT_NEAR(avg, (0 + 0.5f + 1.0f + 1.5f + 0.25f + 0.75f + 1.25f + 1.499f) / 8.0f, 0.0001f);

    // Second frame different resolution to ensure resize logic works
    ctx.received = false;
    RawDepthFrame raw2{};
    raw2.sensorId = "TestSensor";
    raw2.width = 2;
    raw2.height = 1;
    raw2.timestamp_ns = 999ULL;
    raw2.data = {1500, 0};
    pm.processRawDepthFrame(raw2);
    ASSERT_TRUE(ctx.received);
    EXPECT_EQ(ctx.frame.heightMap.width, 2);
    EXPECT_EQ(ctx.frame.heightMap.height, 1);
    ASSERT_EQ(ctx.frame.heightMap.data.size(), 2u);
    EXPECT_FLOAT_EQ(ctx.frame.heightMap.data[0], 1.5f);
    EXPECT_FLOAT_EQ(ctx.frame.heightMap.data[1], 0.0f);
    EXPECT_EQ(ctx.frame.frame_id, 1u);
}
