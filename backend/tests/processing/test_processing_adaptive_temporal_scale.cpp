#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>
#include <cmath>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static RawDepthFrame mk(uint32_t w, uint32_t h, const std::vector<uint16_t>& d){ RawDepthFrame f; f.sensorId="atmp"; f.width=w; f.height=h; f.data=d; f.timestamp_ns=0; return f; }

static float meanAbsHorizontalDiff(const caldera::backend::common::WorldFrame& wf){ if(wf.heightMap.width<2) return 0.f; double acc=0; int c=0; for(uint32_t y=0;y<wf.heightMap.height;++y){ for(uint32_t x=1;x<wf.heightMap.width;++x){ float a=wf.heightMap.data[y*wf.heightMap.width+x-1]; float b=wf.heightMap.data[y*wf.heightMap.width+x]; acc += std::abs(a-b); ++c; } } return c? float(acc/c):0.f; }

TEST(AdaptiveTemporalScaleTest, BlendsWhenUnstable) {
    setenv("CALDERA_PROCESSING_STABILITY_METRICS","1",1);
    setenv("CALDERA_ADAPTIVE_MODE","2",1); // enable adaptive logic path
    setenv("CALDERA_ADAPTIVE_STABILITY_MIN","0.99",1); // treat pattern as unstable
    setenv("CALDERA_ADAPTIVE_VARIANCE_MAX","0.00001",1);
    setenv("CALDERA_ADAPTIVE_ON_STREAK","1",1);
    setenv("CALDERA_ENABLE_SPATIAL_FILTER","1",1); // ensure spatial active baseline
    setenv("CALDERA_ADAPTIVE_TEMPORAL_SCALE","3",1); // strong temporal blend

    auto &loggerSingleton = caldera::backend::common::Logger::instance();
    if(!loggerSingleton.isInitialized()) loggerSingleton.initialize("logs/test_adaptive_temporal.log");

    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    caldera::backend::common::WorldFrame wf0, wf1;
    pm.setWorldFrameCallback([&](const auto& wf){ if(wf.frame_id==0) wf0=wf; else wf1=wf; });

    // Alternating pattern to create instability
    std::vector<uint16_t> depths = {100,400,100,400,100,400,100,400};
    pm.processRawDepthFrame(mk(8,1,depths));
    float d0 = meanAbsHorizontalDiff(wf0);
    pm.processRawDepthFrame(mk(8,1,depths));
    float d1 = meanAbsHorizontalDiff(wf1);

    auto metrics = pm.lastStabilityMetrics();
    // With temporal scaling, second frame should show adaptiveTemporalBlend and reduced diff relative baseline
    EXPECT_EQ(metrics.adaptiveTemporalBlend, 1.0f);
    EXPECT_LE(d1, d0) << "Temporal blend did not reduce or maintain variability";

    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    unsetenv("CALDERA_ADAPTIVE_MODE");
    unsetenv("CALDERA_ADAPTIVE_STABILITY_MIN");
    unsetenv("CALDERA_ADAPTIVE_VARIANCE_MAX");
    unsetenv("CALDERA_ADAPTIVE_ON_STREAK");
    unsetenv("CALDERA_ENABLE_SPATIAL_FILTER");
    unsetenv("CALDERA_ADAPTIVE_TEMPORAL_SCALE");
}
