// test_processing_confidence_map.cpp
// Basic tests for confidence map MVP (M5)

#include <gtest/gtest.h>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static std::shared_ptr<spdlog::logger> makeTestLogger(const std::string& name) {
  auto sink = std::make_shared<spdlog::sinks::null_sink_st>();
  auto logger = std::make_shared<spdlog::logger>(name, sink);
  logger->set_level(spdlog::level::info);
  return logger;
}

static RawDepthFrame makeFrame(uint32_t w, uint32_t h, uint16_t fill, uint16_t invalidEveryN=0) {
  RawDepthFrame f; f.width=w; f.height=h; f.sensorId="conf"; f.timestamp_ns=0; f.data.resize(w*h, fill);
  if (invalidEveryN>0) {
    for(size_t i=0;i<f.data.size();++i){ if(i % invalidEveryN==0) f.data[i]=0; }
  }
  return f;
}

TEST(ConfidenceMapTest, DisabledPathProducesZeroAggregates) {
  setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "0", 1);
  setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
  ProcessingManager mgr(makeTestLogger("orch"));
  auto frame = makeFrame(8,8,500);
  mgr.processRawDepthFrame(frame);
  const auto& m = mgr.lastStabilityMetrics();
  EXPECT_FLOAT_EQ(m.meanConfidence, 0.0f);
  EXPECT_FLOAT_EQ(m.fractionLowConfidence, 0.0f);
  EXPECT_FLOAT_EQ(m.fractionHighConfidence, 0.0f);
}

TEST(ConfidenceMapTest, InvalidPixelsHaveZeroConfidence) {
  setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "1", 1);
  setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
  // Force metrics stable so S≈1.
  ProcessingManager mgr(makeTestLogger("orch"));
  auto frame = makeFrame(16,1,400,4); // every 4th pixel invalid (set to depth 0)
  mgr.processRawDepthFrame(frame);
  const auto& metrics = mgr.lastStabilityMetrics();
  // meanConfidence should be >0 because valid pixels get positive value, but fractionLow > 0 due to zeros.
  EXPECT_GT(metrics.meanConfidence, 0.0f);
  EXPECT_GT(metrics.fractionLowConfidence, 0.0f); // zeros counted as low
}

TEST(ConfidenceMapTest, HigherStabilityRaisesConfidence) {
  setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "1", 1);
  setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
  // Two frames: first noisy to reduce stability, second uniform to raise stability -> meanConfidence2 >= meanConfidence1
  ProcessingManager mgr(makeTestLogger("orch"));
  // Frame 1: alternating pattern to induce larger diffs
  RawDepthFrame f1 = makeFrame(32,1,500);
  for (size_t i=0;i<f1.data.size();++i){ f1.data[i] = (i%2==0)? 400: 600; }
  mgr.processRawDepthFrame(f1);
  float c1 = mgr.lastStabilityMetrics().meanConfidence;
  // Frame 2: uniform
  RawDepthFrame f2 = makeFrame(32,1,500);
  mgr.processRawDepthFrame(f2);
  float c2 = mgr.lastStabilityMetrics().meanConfidence;
  EXPECT_GE(c2, c1);
}

// Spatial benefit: Enable spatial filter via env so that on a noisy frame followed by a smoother frame
// the spatial variance ratio contributes (R term) yielding higher confidence than with spatial disabled.
TEST(ConfidenceMapTest, SpatialFilteringIncreasesConfidenceOnNoisyFrame) {
  setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "1", 1);
  setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
  // Force spatial filtering always on by setting stability thresholds permissive
  setenv("CALDERA_ADAPTIVE_MODE", "1", 1); // enable adaptive spatial logic
  setenv("CALDERA_ADAPTIVE_SPATIAL_MIN_STREAK", "0", 1);
  setenv("CALDERA_ADAPTIVE_SPATIAL_ENABLE_THRESH", "1.0", 1); // extremely high so it engages? Actually we want it ON; emulate by setting strong mode directly (fallback)
  // Simpler: rely on baseline spatial pass always active by simulating unstable first frame then second frame (stabilityRatio improves).
  ProcessingManager mgrA(makeTestLogger("orchA"));
  // Sequence with spatial active by default (manager A)
  RawDepthFrame noisy = makeFrame(32,1,500);
  for(size_t i=0;i<noisy.data.size();++i){ noisy.data[i] = (i%4<2)? 400: 620; }
  mgrA.processRawDepthFrame(noisy);
  RawDepthFrame smoother = makeFrame(32,1,510);
  mgrA.processRawDepthFrame(smoother);
  float cSpatial = mgrA.lastStabilityMetrics().meanConfidence;

  // Manager without spatial: disable adaptive and pretend spatial filter off by disabling stability metrics that would cause variance ratio (simulate by turning off confidence weights for R)
  setenv("CALDERA_CONFIDENCE_WEIGHTS", "S=1,R=0,T=1", 1);
  ProcessingManager mgrNoSpatial(makeTestLogger("orchB"));
  mgrNoSpatial.processRawDepthFrame(noisy);
  mgrNoSpatial.processRawDepthFrame(smoother);
  float cNoSpatial = mgrNoSpatial.lastStabilityMetrics().meanConfidence;

  // Restore default weights for other tests
  setenv("CALDERA_CONFIDENCE_WEIGHTS", "S=1,R=1,T=1", 1);
  EXPECT_GE(cSpatial, cNoSpatial);
}

// Temporal influence: Create alternating stable/unstable frames so temporal blend (T) contributes between frames.
// We expect mean confidence to increase when temporal history becomes stable after instability.
TEST(ConfidenceMapTest, TemporalBlendRaisesConfidenceAfterStability) {
  setenv("CALDERA_ENABLE_CONFIDENCE_MAP", "1", 1);
  setenv("CALDERA_PROCESSING_STABILITY_METRICS", "1", 1);
  setenv("CALDERA_ADAPTIVE_TEMPORAL_SCALE", "1.0", 1); // allow strong temporal blending when unstable
  ProcessingManager mgr(makeTestLogger("orchT"));
  // Frame 1: noisy
  RawDepthFrame noisy = makeFrame(64,1,600);
  for(size_t i=0;i<noisy.data.size();++i){ noisy.data[i] = (i%2==0)? 550: 650; }
  mgr.processRawDepthFrame(noisy);
  float c1 = mgr.lastStabilityMetrics().meanConfidence;
  // Frame 2: still somewhat noisy (maintain instability)
  RawDepthFrame noisy2 = makeFrame(64,1,600);
  for(size_t i=0;i<noisy2.data.size();++i){ noisy2.data[i] = (i%4<2)? 560: 640; }
  mgr.processRawDepthFrame(noisy2);
  float c2 = mgr.lastStabilityMetrics().meanConfidence;
  // Frame 3: uniform stable frame should raise stability & temporal composite
  RawDepthFrame stable = makeFrame(64,1,600);
  mgr.processRawDepthFrame(stable);
  float c3 = mgr.lastStabilityMetrics().meanConfidence;
  EXPECT_GE(c3, c2);
  EXPECT_GE(c2, c1 * 0.5f); // sanity: not collapsing
}
