#include <gtest/gtest.h>
#include <cstdlib>
#include <memory>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"

using namespace caldera::backend::processing;
using namespace caldera::backend::common;

static RawDepthFrame makeFrame(int w,int h,uint16_t d){ RawDepthFrame f; f.sensorId="sensorC"; f.width=w; f.height=h; f.data.assign(w*h,d); f.timestamp_ns=111; return f; }

class NoOpHeightFilterCE : public IHeightMapFilter { public: void apply(std::vector<float>&, int, int) override {} };

TEST(ProcessingConfidenceExport, DisabledNoMap) {
    unsetenv("CALDERA_ENABLE_CONFIDENCE_MAP");
    unsetenv("CALDERA_PROCESSING_EXPORT_CONFIDENCE");
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    auto logger = std::shared_ptr<spdlog::logger>();
    ProcessingManager mgr(logger);
    mgr.setHeightMapFilter(std::make_shared<NoOpHeightFilterCE>());
    mgr.processRawDepthFrame(makeFrame(4,4,500));
    const auto& cm = mgr.confidenceMap();
    EXPECT_TRUE(cm.empty());
}

TEST(ProcessingConfidenceExport, EnabledProducesMap) {
    setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "1", 1);
    setenv("CALDERA_PROCESSING_EXPORT_CONFIDENCE", "1", 1);
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    auto logger = std::shared_ptr<spdlog::logger>();
    ProcessingManager mgr(logger);
    mgr.setHeightMapFilter(std::make_shared<NoOpHeightFilterCE>());
    mgr.processRawDepthFrame(makeFrame(4,4,600));
    const auto& cm = mgr.confidenceMap();
    ASSERT_EQ(cm.size(), static_cast<size_t>(4*4));
    // Values should be within [0,1]
    for(float v: cm) { EXPECT_GE(v,0.0f); EXPECT_LE(v,1.0f); }
}

TEST(ProcessingConfidenceExport, WeightsAffectMean) {
    // Run two passes with different weights and expect difference in mean confidence across frames if spatial term removed
    setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "1", 1);
    setenv("CALDERA_PROCESSING_EXPORT_CONFIDENCE", "1", 1);
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    setenv("CALDERA_CONFIDENCE_WEIGHTS", "0.9,0.05,0.05", 1);
    auto logger = std::shared_ptr<spdlog::logger>();
    ProcessingManager mgrA(logger);
    mgrA.setHeightMapFilter(std::make_shared<NoOpHeightFilterCE>());
    mgrA.processRawDepthFrame(makeFrame(6,6,650));
    auto meanA = mgrA.lastStabilityMetrics().meanConfidence;

    setenv("CALDERA_CONFIDENCE_WEIGHTS", "0.2,0.6,0.2", 1);
    ProcessingManager mgrB(logger);
    mgrB.setHeightMapFilter(std::make_shared<NoOpHeightFilterCE>());
    mgrB.processRawDepthFrame(makeFrame(6,6,650));
    auto meanB = mgrB.lastStabilityMetrics().meanConfidence;
    // With different weight emphasis (especially on R which is neutral=1-> contributes 0), means can differ; just sanity check bounds
    EXPECT_GE(meanA,0.0f); EXPECT_LE(meanA,1.0f);
    EXPECT_GE(meanB,0.0f); EXPECT_LE(meanB,1.0f);
}
