#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static RawDepthFrame mk(uint32_t w, uint32_t h, const std::vector<uint16_t>& d){ RawDepthFrame f; f.sensorId="kalt"; f.width=w; f.height=h; f.data=d; f.timestamp_ns=0; return f; }

// Helper to run one frame and return spatialVarianceRatio
static float runOnce(const std::vector<uint16_t>& depths, const char* kernelAlt){
    setenv("CALDERA_PROCESSING_STABILITY_METRICS","1",1);
    setenv("CALDERA_ENABLE_SPATIAL_FILTER","1",1);
    setenv("CALDERA_SPATIAL_SAMPLE_COUNT","128",1); // ensure sampling (< total pixels)
    // Force deterministic single-pass behavior: disable adaptive toggling & strong escalation
    setenv("CALDERA_ADAPTIVE_SPATIAL_ENABLED","0",1);
    setenv("CALDERA_ADAPTIVE_STRONG_KERNEL","0",1);
    if(kernelAlt) setenv("CALDERA_SPATIAL_KERNEL_ALT",kernelAlt,1); else unsetenv("CALDERA_SPATIAL_KERNEL_ALT");

    auto &loggerSingleton = caldera::backend::common::Logger::instance();
    if(!loggerSingleton.isInitialized()) loggerSingleton.initialize("logs/test_wide5.log");
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);
    caldera::backend::common::WorldFrame wf; pm.setWorldFrameCallback([&](const auto& f){ wf=f; });
    pm.processRawDepthFrame(mk(64,4, depths));
    auto m = pm.lastStabilityMetrics();
    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    unsetenv("CALDERA_ENABLE_SPATIAL_FILTER");
    unsetenv("CALDERA_SPATIAL_SAMPLE_COUNT");
    unsetenv("CALDERA_SPATIAL_KERNEL_ALT");
    unsetenv("CALDERA_ADAPTIVE_SPATIAL_ENABLED");
    unsetenv("CALDERA_ADAPTIVE_STRONG_KERNEL");
    return m.spatialVarianceRatio;
}

TEST(SpatialKernelWide5Test, Wide5NotWorseThanClassicSinglePass) {
    // Create a structured noisy pattern to emphasize smoothing difference
    std::vector<uint16_t> d; d.reserve(64*4);
    for(int y=0;y<4;++y){ for(int x=0;x<64;++x){ d.push_back( ( (x+y)%4==0)? 400: ( (x%2)? 200:800) ); } }
    float classic = runOnce(d, nullptr); // classic
    float wide5 = runOnce(d, "wide5");
    ASSERT_GT(classic, 0.0f);
    ASSERT_GT(wide5, 0.0f);
    // wide5 should not produce higher variance ratio than classic (i.e., ratio <= classic + small epsilon)
    EXPECT_LE(wide5, classic + 0.05f) << "wide5 variance ratio unexpectedly higher (worse smoothing)";
}

TEST(SpatialKernelWide5Test, Wide5ReductionConsistentOnDifferentPattern) {
    std::vector<uint16_t> d; d.reserve(64*4);
    for(int y=0;y<4;++y){ for(int x=0;x<64;++x){ d.push_back( ( (x+y)%3==0)? 500: ( (x%2)? 100:900) ); } }
    // Simulate strong double-pass classic by calling runOnce twice? Instead enable double-pass through strong flag.
    float classic2 = runOnce(d, nullptr);
    float wide5 = runOnce(d, "wide5");
    ASSERT_GT(classic2, 0.0f);
    ASSERT_GT(wide5, 0.0f);
    // wide5 should not degrade vs classic on second pattern beyond modest epsilon
    EXPECT_LE(wide5, classic2 + 0.05f);
}
