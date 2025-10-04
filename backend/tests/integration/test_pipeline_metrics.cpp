// Phase 9: Metrics & Observability
// Validates harness-level stats consistency (frames_in/out/published, latency metrics) under normal and drop scenarios.

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "IntegrationHarness.h"
#include "transport/SharedMemoryReader.h"
#include "common/Logger.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;

static void run_basic_metrics(bool inject_drop) {
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.width=16; sc.height=16; sc.fps=40.0; sc.pattern=SyntheticSensorDevice::Pattern::RAMP; sc.sensorId = inject_drop ? "MetricsDrop" : "MetricsBase";
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name = inject_drop ? "/caldera_metrics_drop" : "/caldera_metrics_base"; hc.max_width=32; hc.max_height=32; hc.processing_scale=0.001f;
    ASSERT_TRUE(harness.start(hc));
    if (inject_drop) {
        // Configure dropEveryN=5 after start to simulate upstream production drops
        auto *sensor = harness.syntheticSensor();
        ASSERT_NE(sensor, nullptr);
        SyntheticSensorDevice::FaultInjectionConfig fic; fic.dropEveryN = 5; fic.jitterMaxMs = 0; fic.seed = 0x1234;
        sensor->configureFaultInjection(fic);
    }

    // Optional drop injection: pause briefly mid run to simulate lost upstream frames (no actual drop logic yet, so we simulate by pausing sensor if available in future).
    std::this_thread::sleep_for(500ms);
    // Allow a short settle window for processing/publish to catch up with sensor emission.
    // This avoids transient races where frames_in is ahead of frames_out by 1 while a frame is in-flight.
    auto st = harness.stats();
    auto settle_deadline = std::chrono::steady_clock::now() + 150ms;
    while (std::chrono::steady_clock::now() < settle_deadline) {
        if (st.frames_out == st.frames_in && st.frames_published == st.frames_out) break;
        std::this_thread::sleep_for(10ms);
        st = harness.stats();
    }
    ASSERT_GT(st.frames_in, 0u);
    if (!inject_drop) {
        ASSERT_EQ(st.frames_in, st.frames_out) << "No processing loss expected";
        ASSERT_EQ(st.frames_out, st.frames_published) << "All processed frames should be published";
    } else {
        // Drop occurs before harness frames_in increment (only emitted frames increment frames_in).
        // So frames_in == frames_out == frames_published still holds under dropEveryN.
        EXPECT_EQ(st.frames_in, st.frames_out);
        EXPECT_EQ(st.frames_out, st.frames_published);
    }
    EXPECT_GE(st.last_latency_ns, 0u);
    EXPECT_GT(st.mean_latency_ns, 0.0);
    if (!inject_drop) {
        EXPECT_EQ(st.derived_dropped, 0u);
    } else {
        // Derived dropped counts difference between frames_in and frames_out; with pre-emission drop it stays 0.
        EXPECT_EQ(st.derived_dropped, 0u) << "Derived dropped should remain 0 (drop before frames_in).";
    }

    // p95 sanity:
    // Normally p95 >= mean for light-tailed latency distributions. However with very few samples
    // (we only sleep 500ms @ 40FPS ≈ 20 frames) a single extreme outlier can lift the mean above
    // the (second largest) sample selected as p95 (since with N=20 the 95th percentile index is 18).
    // So we allow p95 < mean as long as it is not dramatically lower (e.g. >60% of mean).
    auto p95 = st.p95_latency_ns;
    if (st.mean_latency_ns > 0.0 && p95 > 0) {
        double mean = st.mean_latency_ns;
        if (p95 < mean) {
            double ratio = static_cast<double>(p95) / mean;
            EXPECT_GT(ratio, 0.60) << "p95 latency too far below mean (mean=" << mean << " ns p95=" << p95 << " ns)";
        } else {
            EXPECT_GE(p95, static_cast<uint64_t>(mean));
        }
        EXPECT_LT(p95 / 1e6, 5.0) << "p95 latency unexpectedly high";
    }

    harness.stop();
}

TEST(PipelineMetrics, BasicConsistency) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/pipeline_metrics.log");
    }
    run_basic_metrics(false);
}

TEST(PipelineMetrics, DropEveryNStatsConsistency) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/pipeline_metrics.log");
    }
    run_basic_metrics(true);
}
