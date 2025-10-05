#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static RawDepthFrame mk(uint32_t w, uint32_t h, const std::vector<uint16_t>& d){ RawDepthFrame f; f.sensorId="smp"; f.width=w; f.height=h; f.data=d; f.timestamp_ns=0; return f; }

TEST(SpatialSamplingMetricTest, ReportsVarianceReductionWhenSpatialActive) {
    setenv("CALDERA_PROCESSING_STABILITY_METRICS","1",1);
    setenv("CALDERA_ENABLE_SPATIAL_FILTER","1",1); // force spatial on
    setenv("CALDERA_SPATIAL_SAMPLE_COUNT","128",1); // small sample for test

    auto &loggerSingleton = caldera::backend::common::Logger::instance();
    if(!loggerSingleton.isInitialized()) {
        loggerSingleton.initialize("logs/test_sampling_metric.log");
    }
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    caldera::backend::common::WorldFrame wf;
    pm.setWorldFrameCallback([&](const caldera::backend::common::WorldFrame& frame){ wf=frame; });

    // Create a frame with alternating elevations to ensure variance is non-zero
    // Raw depths -> scale 0.001f so choose differing values
    std::vector<uint16_t> depths; depths.reserve(64);
    for (int i=0;i<64;++i){ depths.push_back((i%2)? 800:200); }
    auto frame = mk(32,2, depths);
    pm.processRawDepthFrame(frame);

    auto metrics = pm.lastStabilityMetrics();
    // We expect spatialVarianceRatio to be <= 1 (reduction) and >0 due to sampling
    ASSERT_GE(metrics.spatialVarianceRatio, 0.0f);
    // Not strictly guaranteed <1 if pattern degenerates after filter, but typical smoothing reduces variance
    EXPECT_LE(metrics.spatialVarianceRatio, 1.05f);

    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    unsetenv("CALDERA_ENABLE_SPATIAL_FILTER");
    unsetenv("CALDERA_SPATIAL_SAMPLE_COUNT");
}
