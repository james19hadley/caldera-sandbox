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
#include "transport/SharedMemoryReader.h"
#include "common/Checksum.h"
#include "common/Logger.h"

using namespace std::chrono_literals;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::transport::SharedMemoryReader;

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
    SharedMemoryReader reader(logger);
    ASSERT_TRUE(reader.open(hcfg.shm_name, hcfg.max_width, hcfg.max_height));

    // Precompute expected pattern & CRC for integrity
    auto expected = regenerateRamp(scfg.width, scfg.height, hcfg.processing_scale);
    uint32_t expected_crc = caldera::backend::common::crc32(expected);

    const int targetFrames = 25; // enough for percentile estimate
    std::vector<double> latencies_ms;
    latencies_ms.reserve(targetFrames);
    uint64_t last_frame_id = std::numeric_limits<uint64_t>::max();
    int collected = 0;
    auto start_wall = clock::now();
    auto deadline = start_wall + 5s; // safety timeout

    while (collected < targetFrames && clock::now() < deadline) {
        if (auto fv = reader.latest()) {
            if (fv->frame_id != last_frame_id) {
                // Measure latency (now - production timestamp)
                uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
                uint64_t prod_ns = fv->timestamp_ns;
                double dt_ms = static_cast<double>(now_ns - prod_ns) / 1e6;
                if (dt_ms >= 0.0) latencies_ms.push_back(dt_ms); // ignore negative (clock anomaly)

                // Basic integrity checks
                ASSERT_EQ(fv->width, static_cast<uint32_t>(scfg.width));
                ASSERT_EQ(fv->height, static_cast<uint32_t>(scfg.height));
                ASSERT_EQ(fv->float_count, expected.size());
                // Compute CRC over received floats
                uint32_t crc = caldera::backend::common::crc32_bytes(reinterpret_cast<const uint8_t*>(fv->data), fv->float_count * sizeof(float));
                // We allow metadata checksum to be zero (disabled) but recomputed must match expected
                ASSERT_EQ(crc, expected_crc);

                last_frame_id = fv->frame_id;
                ++collected;
            }
        }
        std::this_thread::sleep_for(1ms); // polling interval (small to limit measurement inflation)
    }

    harness.stop();

    ASSERT_EQ(collected, targetFrames) << "Timed out collecting frames for latency test";
    ASSERT_FALSE(latencies_ms.empty());

    double p95_ms = p95(latencies_ms);
    double max_ms = *std::max_element(latencies_ms.begin(), latencies_ms.end());
    double mean_ms = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0) / latencies_ms.size();

    // Assertions (with a small cushion for CI jitter): Plan target P95 < 10ms
    EXPECT_LT(p95_ms, 15.0) << "P95 latency too high (target <10ms soft, <15ms hard): p95=" << p95_ms
                            << " mean=" << mean_ms << " max=" << max_ms;
    EXPECT_LT(max_ms, 25.0) << "Max latency outlier unacceptable: max=" << max_ms;

    // Log distribution summary for visibility
    logger->info("Latency stats ms: count={} mean={:.3f} p95={:.3f} max={:.3f}", latencies_ms.size(), mean_ms, p95_ms, max_ms);
}
