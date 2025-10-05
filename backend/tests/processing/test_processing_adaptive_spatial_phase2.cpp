// M4 Phase 2 adaptive hysteresis & strong mode tests
#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>
#include <vector>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static RawDepthFrame mkFrame(uint32_t w, uint32_t h, const std::vector<uint16_t>& d){ RawDepthFrame f; f.sensorId="adpt2"; f.width=w; f.height=h; f.data=d; f.timestamp_ns=0; return f; }

// Helper re-used: simple mean abs diff horizontally
static float horizDiff(const caldera::backend::common::WorldFrame& wf){ if(wf.heightMap.width<2)return 0.f; double t=0; int c=0; for(uint32_t y=0;y<wf.heightMap.height;++y){ for(uint32_t x=1;x<wf.heightMap.width;++x){ float a=wf.heightMap.data[y*wf.heightMap.width+x-1]; float b=wf.heightMap.data[y*wf.heightMap.width+x]; t+=std::abs(a-b); ++c; } } return c?float(t/c):0.f; }

TEST(AdaptiveSpatialPhase2Test, HysteresisPreventsSingleFrameFlap) {
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    setenv("CALDERA_ADAPTIVE_MODE", "2", 1);
    setenv("CALDERA_ADAPTIVE_STABILITY_MIN", "0.95", 1); // moderately strict
    setenv("CALDERA_ADAPTIVE_VARIANCE_MAX", "0.0005", 1);
    setenv("CALDERA_ADAPTIVE_ON_STREAK", "2", 1); // require two consecutive unstable frames
    setenv("CALDERA_ADAPTIVE_OFF_STREAK", "3", 1);

    auto &L = caldera::backend::common::Logger::instance();
    if(!L.isInitialized()) L.initialize("logs/test_adaptive_phase2.log");
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    std::vector<caldera::backend::common::WorldFrame> frames(3);
    pm.setWorldFrameCallback([&](const caldera::backend::common::WorldFrame& wf){ if (wf.frame_id < frames.size()) frames[wf.frame_id] = wf; });

    // Pattern produces some variance (alternating depth)
    auto noisy = mkFrame(6,1,{100,400,100,400,100,400});
    // First frame: collects metrics only (no previous metrics for decision)
    pm.processRawDepthFrame(noisy);
    // Second frame: only 1 unstable streak so far; should NOT yet enable spatial (because ON_STREAK=2)
    pm.processRawDepthFrame(noisy);
    // Third frame: now prior metrics unstable twice -> spatial should activate
    pm.processRawDepthFrame(noisy);

    auto m3 = pm.lastStabilityMetrics();
    // After third frame adaptiveSpatial must be 1
    EXPECT_EQ(m3.adaptiveSpatial, 1.0f) << "Spatial not enabled after required unstable streak";

    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    unsetenv("CALDERA_ADAPTIVE_MODE");
    unsetenv("CALDERA_ADAPTIVE_STABILITY_MIN");
    unsetenv("CALDERA_ADAPTIVE_VARIANCE_MAX");
    unsetenv("CALDERA_ADAPTIVE_ON_STREAK");
    unsetenv("CALDERA_ADAPTIVE_OFF_STREAK");
}

TEST(AdaptiveSpatialPhase2Test, StrongModeDoublePassReducesVarianceMore) {
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    setenv("CALDERA_ADAPTIVE_MODE", "2", 1);
    // Force adaptive ON quickly and strong threshold easy to hit.
    setenv("CALDERA_ADAPTIVE_STABILITY_MIN", "0.99", 1);
    setenv("CALDERA_ADAPTIVE_VARIANCE_MAX", "0.0001", 1);
    setenv("CALDERA_ADAPTIVE_ON_STREAK", "1", 1); // enable after first classified unstable
    setenv("CALDERA_ADAPTIVE_STRONG_MULT", "1.2", 1); // low multiplier so strong triggers
    setenv("CALDERA_ADAPTIVE_STRONG_STAB_FRACTION", "0.9", 1);
    setenv("CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS", "1", 1);

    auto &L2 = caldera::backend::common::Logger::instance();
    if(!L2.isInitialized()) L2.initialize("logs/test_adaptive_phase2_strong.log");
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    caldera::backend::common::WorldFrame f0, f1, f2;
    pm.setWorldFrameCallback([&](const caldera::backend::common::WorldFrame& wf){ if(wf.frame_id==0) f0=wf; else if(wf.frame_id==1) f1=wf; else f2=wf; });

    // Alternating large amplitude to ensure high variance.
    auto noisy = mkFrame(8,1,{100,600,100,600,100,600,100,600});
    // Frame 0 (collect baseline variance metrics)
    pm.processRawDepthFrame(noisy);
    float d0 = horizDiff(f0);
    // Frame 1 -> adaptive should enable (need previous unstable metrics). Should apply single pass because strong decided based on frame0 metrics (maybe triggers already if large diff)
    pm.processRawDepthFrame(noisy);
    float d1 = horizDiff(f1);
    auto m1 = pm.lastStabilityMetrics();
    // Frame 2 -> with strong pass condition still true; expect further reduction if double-pass engaged.
    pm.processRawDepthFrame(noisy);
    float d2 = horizDiff(f2);
    auto m2 = pm.lastStabilityMetrics();

    EXPECT_GE(d0, d1) << "Single-pass adaptive did not reduce variance";
    EXPECT_GE(d1, d2) << "Strong double-pass did not further reduce variance";
    EXPECT_EQ(m2.adaptiveStrong, 1.0f) << "Strong pass flag not set in metrics";
    EXPECT_EQ(m2.adaptiveSpatial, 1.0f) << "Spatial unexpectedly disabled";

    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    unsetenv("CALDERA_ADAPTIVE_MODE");
    unsetenv("CALDERA_ADAPTIVE_STABILITY_MIN");
    unsetenv("CALDERA_ADAPTIVE_VARIANCE_MAX");
    unsetenv("CALDERA_ADAPTIVE_ON_STREAK");
    unsetenv("CALDERA_ADAPTIVE_STRONG_MULT");
    unsetenv("CALDERA_ADAPTIVE_STRONG_STAB_FRACTION");
    unsetenv("CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS");
}
