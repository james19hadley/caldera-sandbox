// Phase 5: Transport Round-Trip Latency & Integrity Test
// Measures latency from synthetic sensor timestamp (steady_clock at emission)
// to reader acquisition via SharedMemoryReader. Also re-validates content integrity
// using deterministic pattern regeneration + CRC.

#include <gtest/gtest.h>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <thread>
#include <vector>
#include <cmath>

#include "IntegrationHarness.h"
#include "helpers/TestCalderaClient.h"
#include "common/Checksum.h"
#include "common/Logger.h"

using namespace std::chrono_literals;
using caldera::backend::hal::SyntheticSensorDevice;

namespace {

static std::vector<float> regenerateRamp(int w, int h, float scale) {
    std::vector<float> out(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out[static_cast<size_t>(y)*w + x] = static_cast<float>(x + y) * scale;
        }
    }
    return out;
}

double p95(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(std::ceil(0.95 * v.size())) - 1;
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

} // namespace

TEST(TransportLatency, SingleSensorLatencyP95WithinBudget) {
    using clock = std::chrono::steady_clock;

    // Rely on global test logger initialization strategy (other tests may have set level).
    caldera::backend::tests::IntegrationHarness harness;
    SyntheticSensorDevice::Config scfg;
    scfg.width = 16; scfg.height = 16; scfg.fps = 30.0; scfg.pattern = SyntheticSensorDevice::Pattern::RAMP;
    scfg.sensorId = "LatencySensor";
    harness.addSyntheticSensor(scfg);

    caldera::backend::tests::HarnessConfig hcfg;
    hcfg.shm_name = "/caldera_integration_latency";
    hcfg.max_width = 32; hcfg.max_height = 32; // capacity buffer
    hcfg.processing_scale = 0.001f; // deterministic scaling

    ASSERT_TRUE(harness.start(hcfg));

    auto logger = caldera::backend::common::Logger::instance().get("Test.TransportLatency");
    TestCalderaClient client(logger);
    ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{hcfg.shm_name, static_cast<uint32_t>(hcfg.max_width), static_cast<uint32_t>(hcfg.max_height), true, 2000}));

    // Precompute expected pattern & CRC for integrity
    auto expected = regenerateRamp(scfg.width, scfg.height, hcfg.processing_scale);
    uint32_t expected_crc = caldera::backend::common::crc32(expected);

    const int targetFrames = 25; // enough for percentile estimate
    uint64_t last_frame_id = std::numeric_limits<uint64_t>::max();
    int collected = 0;
    auto start_wall = clock::now();
    auto deadline = start_wall + 5s;
    while (collected < targetFrames && clock::now() < deadline) {
        if (auto fv = client.latest()) {
            if (fv->frame_id != last_frame_id) {
                ASSERT_EQ(fv->width, static_cast<uint32_t>(scfg.width));
                ASSERT_EQ(fv->height, static_cast<uint32_t>(scfg.height));
                ASSERT_EQ(fv->float_count, expected.size());
                uint32_t crc = caldera::backend::common::crc32_bytes(reinterpret_cast<const uint8_t*>(fv->data), fv->float_count * sizeof(float));
                ASSERT_EQ(crc, expected_crc);
                last_frame_id = fv->frame_id;
                ++collected;
            }
        }
        std::this_thread::sleep_for(1ms);
    }

    harness.stop();

    ASSERT_EQ(collected, targetFrames) << "Timed out collecting frames for latency test";
    auto ls = client.latencyStats();
    ASSERT_GT(ls.count, 0u);
    double p95_ms = ls.p95_ms;
    double max_ms = ls.max_ms;
    double mean_ms = ls.mean_ms;

    // Assertions: With a 30 FPS synthetic sensor (frame period ~33ms) and polling-based capture,
    // end-to-end latency (timestamp at production -> consumer acquisition) will often fall within
    // one frame period. Empirically we observe mean ~16ms (mid-period) and p95 low 30s.
    // Set pragmatic thresholds leaving headroom for CI jitter.
    EXPECT_LT(p95_ms, 40.0) << "P95 latency too high (expected <40ms for 30fps pipeline): p95=" << p95_ms
                            << " mean=" << mean_ms << " max=" << max_ms;
    EXPECT_LT(max_ms, 60.0) << "Max latency outlier unacceptable: max=" << max_ms;

    // Log distribution summary for visibility
    logger->info("Latency stats ms: count={} mean={:.3f} p95={:.3f} max={:.3f}", ls.count, mean_ms, p95_ms, max_ms);
}
