// Phase 7: End-to-End Pipeline Throughput & Coverage Test
// Uses SyntheticSensorDevice -> ProcessingManager -> SharedMemoryTransportServer -> SharedMemoryReader
// Validates: published frame count near theoretical target and reader coverage ratio above threshold.

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <atomic>

#include "IntegrationHarness.h"
#include "helpers/TestCalderaClient.h"
#include "common/Logger.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;

namespace {
struct ThroughputCase { double fps; double duration_sec; double min_published_ratio; double min_coverage_ratio; };

void runThroughputCase(const ThroughputCase& tc) {
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.width=32; sc.height=32; sc.fps=tc.fps; sc.pattern=SyntheticSensorDevice::Pattern::RAMP; sc.sensorId = "TP" + std::to_string((int)tc.fps);
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name = "/caldera_throughput_phase7_" + std::to_string((int)tc.fps); hc.max_width=64; hc.max_height=64; hc.processing_scale = 0.001f;
    ASSERT_TRUE(harness.start(hc));

    auto log = caldera::backend::common::Logger::instance().get("Test.Phase7.Throughput." + std::to_string((int)tc.fps));
    TestCalderaClient client(log);
    ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{hc.shm_name, static_cast<uint32_t>(hc.max_width), static_cast<uint32_t>(hc.max_height), true, 2000}));

    const auto test_duration = std::chrono::duration<double>(tc.duration_sec);
    auto t_start = std::chrono::steady_clock::now();
    auto t_end = t_start + test_duration;
    uint64_t last_id = std::numeric_limits<uint64_t>::max();
    uint64_t observed = 0;
    while (std::chrono::steady_clock::now() < t_end) {
        if (auto opt = client.latest()) {
            if (opt->frame_id != last_id) {
                last_id = opt->frame_id;
                ++observed;
            }
        }
        std::this_thread::sleep_for(1ms); // polling cadence intentionally modest
    }
    // Give final frames a brief chance to be seen
    std::this_thread::sleep_for(50ms);
    uint64_t published = harness.framesPublished();
    harness.stop();

    double expected_theoretical = tc.fps * tc.duration_sec;
    // Allow some scheduling overhead: expect published >= fps*duration*min_published_ratio
    double min_published = expected_theoretical * tc.min_published_ratio;
    double coverage = published > 0 ? static_cast<double>(observed) / static_cast<double>(published) : 0.0;

    log->info("Phase7 case fps={} dur={}s published={} expected~{} observed={} coverage={:.2f}",
              tc.fps, tc.duration_sec, published, expected_theoretical, observed, coverage);

    EXPECT_GE(published, static_cast<uint64_t>(min_published))
        << "Published frames below expectation (fps scheduling regression?)";
    EXPECT_GE(coverage, tc.min_coverage_ratio)
        << "Reader coverage too low for latest()-snapshot semantics: coverage=" << coverage;
    // Monotonicity implicit via observed counting; assert we saw at least some frames.
    EXPECT_GT(observed, 0u);
    // Explicit monotonic/continuity style assertion: last observed id should be
    // close to (published-1). Allow a tiny slack (<=2) for race at stop boundary.
    if (published > 0 && last_id != std::numeric_limits<uint64_t>::max()) {
        // Allow a dynamic slack proportional to fps burstiness (higher fps => more chance latest() skips tail frames)
        uint64_t slack = tc.fps <= 60.0 ? 3 : 10; // heuristic; keeps invariant meaningful without flakiness
        if (published > slack) {
            EXPECT_GE(last_id, published - slack) << "Last observed frame id too far behind published count (slack=" << slack << ")";
        }
    }
    auto st = client.stats();
    log->info("Client stats: distinct={} observed={} max_gap={} skipped={} checksum_present={} verified={} mismatch={} latency_samples={}",
              st.distinct_frames, st.frames_observed, st.max_gap, st.total_skipped, st.checksum_present, st.checksum_verified, st.checksum_mismatch, st.latency_samples);
}
} // namespace

TEST(PipelineThroughput, Fps30Baseline) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/pipeline_throughput.log");
    }
    ThroughputCase tc{30.0, 2.0, 0.90, 0.85}; // expect 90% published of theoretical, 85% coverage observed
    runThroughputCase(tc);
}

TEST(PipelineThroughput, Fps120Stress) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/pipeline_throughput.log");
    }
    ThroughputCase tc{120.0, 2.0, 0.85, 0.70}; // more lenient coverage at higher rate
    runThroughputCase(tc);
}
