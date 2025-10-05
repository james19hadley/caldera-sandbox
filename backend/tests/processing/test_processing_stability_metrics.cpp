#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <cstdlib>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static RawDepthFrame makeFrame(const std::string& id, uint32_t w, uint32_t h, const std::vector<uint16_t>& depths) {
    RawDepthFrame f; f.sensorId=id; f.width=w; f.height=h; f.data=depths; f.timestamp_ns=0; return f; }

TEST(StabilityMetricsTest, EnabledComputesValues) {
    // Enable metrics
    setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
    caldera::backend::common::Logger::instance().initialize("logs/test_stability.log");
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    // Simple frame with gradient depths so variance proxy non-zero
    RawDepthFrame f1 = makeFrame("s", 4, 1, {100,200,300,400}); // scaled ~ 0.1..0.4
    pm.processRawDepthFrame(f1);
    auto m1 = pm.lastStabilityMetrics();
    EXPECT_EQ(m1.frameId, 0u); // captured before increment in instrumentation block? (frameId set to current value pre-increment)
    EXPECT_EQ(m1.width, 4u);
    EXPECT_NEAR(m1.avgVariance, 0.1f, 0.2f); // rough bound
    EXPECT_GT(m1.procTotalMs, 0.0f);
    EXPECT_GE(m1.stabilityRatio, 0.0f); EXPECT_LE(m1.stabilityRatio, 1.0f);

    // Second frame different pattern to adjust EMA
    RawDepthFrame f2 = makeFrame("s", 4, 1, {100,400,100,400});
    pm.processRawDepthFrame(f2);
    auto m2 = pm.lastStabilityMetrics();
    EXPECT_EQ(m2.frameId, 1u);
    EXPECT_NE(m1.avgVariance, m2.avgVariance); // EMA should update
    EXPECT_NEAR(m2.procTotalMs, m2.buildMs + m2.filterMs + m2.fuseMs, 2.0f); // within 2ms slack

    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
}

TEST(StabilityMetricsTest, DisabledNoWork) {
    unsetenv("CALDERA_PROCESSING_STABILITY_METRICS");
    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);
    RawDepthFrame f = makeFrame("s", 2,1,{100,200});
    pm.processRawDepthFrame(f);
    auto m = pm.lastStabilityMetrics();
    // Because disabled, frameId will remain 0 after first frame instrumentation sets but we cannot assert timings >0 reliably
    // We instead assert defaults: width/height either zero or set only if enabled; since disabled they remain zero.
    EXPECT_EQ(m.width, 0u);
    EXPECT_EQ(m.height, 0u);
}
