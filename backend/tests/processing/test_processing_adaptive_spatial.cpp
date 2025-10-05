#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static RawDepthFrame mk(uint32_t w, uint32_t h, std::initializer_list<uint16_t> d){ RawDepthFrame f; f.sensorId="adpt"; f.width=w; f.height=h; f.data=d; f.timestamp_ns=0; return f; }

// Helper to compute simple horizontal mean abs diff for result frame
static float simpleDiff(const caldera::backend::common::WorldFrame& wf){ if(wf.heightMap.width<2)return 0; double t=0; int c=0; for(uint32_t y=0;y<wf.heightMap.height;++y){ for(uint32_t x=1;x<wf.heightMap.width;++x){ float a=wf.heightMap.data[y*wf.heightMap.width+x-1]; float b=wf.heightMap.data[y*wf.heightMap.width+x]; t+=std::abs(a-b); ++c; } } return c?float(t/c):0; }

TEST(AdaptiveSpatialTest, EngagesOnLowStability) {
    // Enable metrics and adaptive mode
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    setenv("CALDERA_ADAPTIVE_MODE", "2", 1);
    // Force thresholds high so first frame (no previous metrics) won't spatial filter; second frame should trigger
    setenv("CALDERA_ADAPTIVE_STABILITY_MIN", "0.99", 1); // treat <0.99 as low
    setenv("CALDERA_ADAPTIVE_VARIANCE_MAX", "0.00001", 1); // tiny variance tolerance
    // Ensure spatial can activate after first unstable classification (so 2nd frame filters)
    setenv("CALDERA_ADAPTIVE_ON_STREAK", "1", 1);

    auto &loggerSingleton = caldera::backend::common::Logger::instance();
    if(!loggerSingleton.isInitialized()) {
        loggerSingleton.initialize("logs/test_adaptive.log");
    }
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    caldera::backend::common::WorldFrame wf0, wf1, wf2;
    pm.setWorldFrameCallback([&](const caldera::backend::common::WorldFrame& wf){ if(wf.frame_id==0) wf0=wf; else if(wf.frame_id==1) wf1=wf; else wf2=wf; });

    auto pattern = mk(5,1,{100,400,100,400,100});
    // Frame 0: baseline (no prior metrics)
    pm.processRawDepthFrame(pattern);
    float d0 = simpleDiff(wf0);
    // Frame 1: adaptive may decide to enable (depending on thresholds) but diff might remain same after single smoothing on alternating pattern
    pm.processRawDepthFrame(pattern);
    float d1 = simpleDiff(wf1);
    // Frame 2: after one active frame, smoothing should accumulate effect (temporal + spatial) -> expect reduction vs baseline
    pm.processRawDepthFrame(pattern);
    float d2 = simpleDiff(wf2);

    auto metrics = pm.lastStabilityMetrics();
    EXPECT_GT(d0, d2) << "Adaptive spatial filtering across frames did not reduce variability";
    EXPECT_GE(d0, d1) << "Intermediate frame unexpectedly noisier than baseline";
    // We don't require d0>d1 strictly (alternating pattern may remain), but final should be reduced.
    EXPECT_EQ(metrics.adaptiveSpatial, 1.0f) << "Adaptive spatial not active by third frame";

    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    unsetenv("CALDERA_ADAPTIVE_MODE");
    unsetenv("CALDERA_ADAPTIVE_STABILITY_MIN");
    unsetenv("CALDERA_ADAPTIVE_VARIANCE_MAX");
    unsetenv("CALDERA_ADAPTIVE_ON_STREAK");
}
