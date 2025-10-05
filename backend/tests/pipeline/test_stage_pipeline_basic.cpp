#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>
#include <random>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;

namespace { 
static RawDepthFrame makeDeterministicFrame(const std::string& id, uint32_t w, uint32_t h, uint32_t seed){
    RawDepthFrame f; f.sensorId=id; f.width=w; f.height=h; f.timestamp_ns=0; f.data.resize(w*h);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint16_t> dist(100,1500);
    for(auto &px: f.data) px = dist(rng);
    return f;
}
}

// Basic smoke test: pipeline stages run, produce a fused frame, metrics populated, confidence (if enabled) sized.
TEST(StagePipelineBasic, RunsAndProducesFrame){
    caldera::backend::common::Logger::instance().initialize("logs/test/stage_pipeline_basic.log");
    unsetenv("CALDERA_PROCESSING_PIPELINE"); // allow default auto pipeline
    unsetenv("CALDERA_ENABLE_SPATIAL_FILTER");
    unsetenv("CALDERA_STAGE_EXEC_ENABLE"); // deprecated
    RawDepthFrame frame = makeDeterministicFrame("s", 32, 24, 42);

    WorldFrame captured; bool got=false;
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);
    pm.setWorldFrameCallback([&](const WorldFrame& wf){ captured = wf; got=true; });
    for(int i=0;i<3;++i) pm.processRawDepthFrame(frame); // run a few frames for metrics EMA

    ASSERT_TRUE(got);
    EXPECT_EQ(captured.heightMap.width, 32u);
    EXPECT_EQ(captured.heightMap.height, 24u);
    ASSERT_EQ(captured.heightMap.data.size(), size_t(32*24));
    size_t finite=0; for(float v: captured.heightMap.data) if(std::isfinite(v)) ++finite;
    EXPECT_GT(finite, 0u);

    const auto &metrics = pm.lastStabilityMetrics();
    EXPECT_EQ(metrics.width, 32u);
    EXPECT_EQ(metrics.height, 24u);
    EXPECT_GE(metrics.buildMs, 0.f);
    EXPECT_GE(metrics.fuseMs, 0.f);
    EXPECT_GE(metrics.procTotalMs, metrics.buildMs);
}
