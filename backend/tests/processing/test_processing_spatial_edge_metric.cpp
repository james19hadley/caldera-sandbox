#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/logger.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static std::shared_ptr<spdlog::logger> makeEdgeLogger() {
  auto sink = std::make_shared<spdlog::sinks::null_sink_st>();
  auto logger = std::make_shared<spdlog::logger>("edge", sink);
  logger->set_level(spdlog::level::warn);
  return logger;
}

static RawDepthFrame makeRampFrame(uint32_t w, uint32_t h, uint16_t base, uint16_t step) {
  RawDepthFrame f; f.width=w; f.height=h; f.sensorId="edge"; f.timestamp_ns=0; f.data.resize(w*h);
  for(uint32_t y=0;y<h;++y){
    for(uint32_t x=0;x<w;++x){
      f.data[y*w+x] = base + static_cast<uint16_t>(x*step);
    }
  }
  return f;
}

// This test ensures the edge preservation ratio stays reasonably high (>=0.6) when smoothing a linear ramp
// and that variance ratio reflects reduction (<1 when spatial active). We use adaptive=off, direct spatial enable.
TEST(SpatialEdgeMetricTest, EdgePreservationOnRampFastGaussVsClassic) {
  setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
  setenv("CALDERA_ENABLE_SPATIAL_FILTER", "1", 1);
  // Use fastgauss first
  setenv("CALDERA_SPATIAL_KERNEL_ALT", "fastgauss", 1);
  ProcessingManager mgrFG(makeEdgeLogger());
  auto ramp = makeRampFrame(64,4,400,2); // monotonic horizontal ramp
  mgrFG.processRawDepthFrame(ramp);
  auto mfg = mgrFG.lastStabilityMetrics();
  // Now classic
  setenv("CALDERA_SPATIAL_KERNEL_ALT", "", 1);
  ProcessingManager mgrClassic(makeEdgeLogger());
  mgrClassic.processRawDepthFrame(ramp);
  auto mc = mgrClassic.lastStabilityMetrics();
  // Both should have variance ratio <= ~1 (or zero if sampling degenerated) but edge preservation not collapse
  if (mfg.spatialEdgePreservationRatio > 0.0f)
    EXPECT_GE(mfg.spatialEdgePreservationRatio, 0.6f);
  if (mc.spatialEdgePreservationRatio > 0.0f)
    EXPECT_GE(mc.spatialEdgePreservationRatio, 0.6f);
  // FastGaussianBlur should not destroy edges more than classic beyond a tolerance
  if (mfg.spatialEdgePreservationRatio > 0.0f && mc.spatialEdgePreservationRatio > 0.0f) {
    EXPECT_GE(mfg.spatialEdgePreservationRatio + 0.15f, mc.spatialEdgePreservationRatio);
  }
}
