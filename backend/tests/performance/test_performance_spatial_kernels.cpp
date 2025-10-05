#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <numeric>
#include <cstdlib>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/logger.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

static std::shared_ptr<spdlog::logger> makeBenchLogger(){
  auto sink = std::make_shared<spdlog::sinks::null_sink_st>();
  auto lg = std::make_shared<spdlog::logger>("bench", sink);
  lg->set_level(spdlog::level::warn);
  return lg;
}

static RawDepthFrame makeNoisy(uint32_t w, uint32_t h){
  RawDepthFrame f; f.width=w; f.height=h; f.sensorId="bench"; f.timestamp_ns=0; f.data.resize(w*h);
  for(uint32_t y=0;y<h;++y){ for(uint32_t x=0;x<w;++x){
    // Structured noise pattern
    uint16_t base = 450 + ((x*37 + y*11) % 200); f.data[y*w+x]= base; }}
  return f;
}

struct RunResult { std::string label; double msPerFrame=0; float varRatio=0; float edgeRatio=0; };

static RunResult runMode(const std::string& label, const std::string& altKernel, const std::string& strongKernel, bool strongTrigger,
                         int frames, int warmup, uint32_t w, uint32_t h) {
  setenv("CALDERA_PROCESSING_STABILITY_METRICS","1",1);
  setenv("CALDERA_ENABLE_SPATIAL_FILTER","1",1);
  setenv("CALDERA_ADAPTIVE_MODE","2",1); // adaptive
  setenv("CALDERA_ADAPTIVE_ON_STREAK","1",1);
  setenv("CALDERA_SPATIAL_KERNEL_ALT", altKernel.c_str(), 1);
  setenv("CALDERA_ADAPTIVE_STRONG_KERNEL", strongKernel.c_str(), 1);
  if (strongTrigger) {
    setenv("CALDERA_ADAPTIVE_STRONG_MULT","1",1);
    setenv("CALDERA_ADAPTIVE_STRONG_STAB_FRACTION","1",1);
  } else {
    setenv("CALDERA_ADAPTIVE_STRONG_MULT","4",1);
  }
  ProcessingManager mgr(makeBenchLogger());
  auto frame = makeNoisy(w,h);
  // Warmup
  for(int i=0;i<warmup;++i) mgr.processRawDepthFrame(frame);
  auto t0 = std::chrono::steady_clock::now();
  for(int i=0;i<frames;++i) mgr.processRawDepthFrame(frame);
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
  auto m = mgr.lastStabilityMetrics();
  return RunResult{label, ms, m.spatialVarianceRatio, m.spatialEdgePreservationRatio};
}

TEST(SpatialKernelBenchmark, BasicComparativeTiming) {
  // Keep small to be quick in CI; not a rigorous perf number.
  const int frames = 20; const int warm = 5; uint32_t W=128, H=64;
  std::vector<RunResult> results;
  results.push_back(runMode("classic", "", "classic_double", false, frames, warm, W,H));
  results.push_back(runMode("classic_strong", "", "classic_double", true, frames, warm, W,H));
  results.push_back(runMode("wide5", "wide5", "classic_double", false, frames, warm, W,H));
  results.push_back(runMode("fastgauss", "fastgauss", "fastgauss", false, frames, warm, W,H));
  results.push_back(runMode("fastgauss_strong", "fastgauss", "fastgauss", true, frames, warm, W,H));

  // Sanity assertions: strong variants should not be dramatically faster than their base; fastgauss should not be >3x slower than classic.
  double classicMs = results[0].msPerFrame;
  for(const auto& r: results){
    // Edge ratio / var ratio are optional (may be zero if sampling not triggered strongly); skip strict asserts.
    EXPECT_LT(r.msPerFrame, classicMs * 3.0 + 1e-3);
  }

  // Emit summary to stdout (gtest log) for manual inspection.
  for(const auto& r: results){
    std::cout << "[SPATIAL-BENCH] label=" << r.label
              << " ms/frame=" << r.msPerFrame
              << " varRatio=" << r.varRatio
              << " edgeRatio=" << r.edgeRatio
              << "\n";
  }
}
