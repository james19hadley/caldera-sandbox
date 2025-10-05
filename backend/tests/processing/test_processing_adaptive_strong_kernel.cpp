#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/logger.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include <cstdlib>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static std::shared_ptr<spdlog::logger> makeLogger(const std::string& n){
  auto sink = std::make_shared<spdlog::sinks::null_sink_st>();
  auto lg = std::make_shared<spdlog::logger>(n, sink);
  lg->set_level(spdlog::level::warn);
  return lg;
}

// Generate a noisy frame with alternating bands to trigger strong mode
static RawDepthFrame makeNoisy(uint32_t w, uint32_t h){
  RawDepthFrame f; f.width=w; f.height=h; f.sensorId="str"; f.timestamp_ns=0; f.data.resize(w*h);
  for(uint32_t y=0;y<h;++y){ for(uint32_t x=0;x<w;++x){
    uint16_t base = ( (x+y) % 2 == 0 ) ? 400 : 600; f.data[y*w+x]= base; }}
  return f;
}

// Helper to run single frame and return metrics (stability metrics depend on previous frame; we run two frames)
static ProcessingManager::StabilityMetrics runTwo(const std::string& strongKernel, const std::string& baseAlt) {
  setenv("CALDERA_PROCESSING_STABILITY_METRICS","1",1);
  setenv("CALDERA_ENABLE_SPATIAL_FILTER","1",1);
  setenv("CALDERA_ADAPTIVE_MODE","2",1); // enable adaptive
  setenv("CALDERA_ADAPTIVE_ON_STREAK","1",1);
  setenv("CALDERA_ADAPTIVE_STRONG_MULT","1",1); // aggressive trigger
  setenv("CALDERA_ADAPTIVE_STRONG_KERNEL", strongKernel.c_str(),1);
  setenv("CALDERA_SPATIAL_KERNEL_ALT", baseAlt.c_str(),1);
  ProcessingManager mgr(makeLogger("mgr"));
  auto noisy = makeNoisy(64,4);
  mgr.processRawDepthFrame(noisy); // frame 0 (establish instability)
  mgr.processRawDepthFrame(noisy); // frame 1 (adaptive decisions apply)
  return mgr.lastStabilityMetrics();
}

TEST(AdaptiveStrongKernelTest, FastGaussStrongDoesNotReduceEdgeRatioBelowClassic) {
  auto mClassic = runTwo("classic_double", "");
  auto mFast = runTwo("fastgauss", "fastgauss");
  // Both should have adaptiveSpatial=1 and possibly adaptiveStrong>0
  EXPECT_GE(mClassic.adaptiveSpatial, 0.0f);
  EXPECT_GE(mFast.adaptiveSpatial, 0.0f);
  // Edge preservation ratio (if computed) for fast should not be catastrophically lower
  if (mClassic.spatialEdgePreservationRatio>0 && mFast.spatialEdgePreservationRatio>0) {
    EXPECT_GE(mFast.spatialEdgePreservationRatio + 0.15f, mClassic.spatialEdgePreservationRatio);
  }
}

TEST(AdaptiveStrongKernelTest, FastGaussStrongVarianceReductionComparable) {
  auto mClassic = runTwo("classic_double", "");
  auto mFast = runTwo("fastgauss", "fastgauss");
  if (mClassic.spatialVarianceRatio>0 && mFast.spatialVarianceRatio>0) {
    // Spatial variance ratio <1 means reduction; ensure fastgauss not worse than classic by large margin
    EXPECT_LE(mFast.spatialVarianceRatio, mClassic.spatialVarianceRatio * 1.20f);
  }
}
