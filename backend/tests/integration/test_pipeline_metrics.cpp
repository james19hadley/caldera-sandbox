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
    auto st = harness.stats();
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

    // p95 sanity: should be >= mean (or zero if insufficient samples) and below a generous upper bound (5ms at this small size)
    auto p95 = st.p95_latency_ns;
    if (st.mean_latency_ns > 0.0 && p95 > 0) {
        EXPECT_GE(p95, static_cast<uint64_t>(st.mean_latency_ns));
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
