// Phase 6 (extended): Jitter + Frame Drop Fault Injection
// Ensures pipeline stability with timing variance and sensor-level frame loss.

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <limits>

#include "IntegrationHarness.h"
#include "helpers/TestCalderaClient.h"

using namespace std::chrono_literals;
using caldera::backend::hal::SyntheticSensorDevice;

TEST(FaultInjection, JitterAndDropBehavior) {
    caldera::backend::tests::IntegrationHarness harness;
    SyntheticSensorDevice::Config scfg; scfg.width=16; scfg.height=16; scfg.fps=60.0; scfg.pattern = SyntheticSensorDevice::Pattern::RAMP; scfg.sensorId="JitterDropSensor";
    harness.addSyntheticSensor(scfg);
    caldera::backend::tests::HarnessConfig hcfg; hcfg.shm_name="/caldera_integration_fault_jitter"; hcfg.max_width=32; hcfg.max_height=32; hcfg.processing_scale=0.001f;
    ASSERT_TRUE(harness.start(hcfg));
    auto* sensor = harness.syntheticSensor(0);
    ASSERT_NE(sensor, nullptr);
    sensor->configureFaultInjection({4, 3, 0x1234}); // drop every 4th produced frame, jitter up to 3ms

    auto logger = caldera::backend::common::Logger::instance().get("Test.FaultInjection");
    TestCalderaClient client(logger);
    ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{hcfg.shm_name, static_cast<uint32_t>(hcfg.max_width), static_cast<uint32_t>(hcfg.max_height), true, 2000}));

    const int targetEmitted = 40; // collect 40 delivered frames
    std::vector<uint64_t> frameIds; frameIds.reserve(targetEmitted);
    uint64_t last_id = std::numeric_limits<uint64_t>::max();
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((int)frameIds.size() < targetEmitted && std::chrono::steady_clock::now() < deadline) {
        if (auto fv = client.latest()) {
            if (fv->frame_id != last_id) {
                frameIds.push_back(fv->frame_id);
                last_id = fv->frame_id;
            }
        }
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_EQ((int)frameIds.size(), targetEmitted) << "Timed out collecting emitted frames";

    for (size_t i = 1; i < frameIds.size(); ++i) {
        ASSERT_EQ(frameIds[i], frameIds[i-1] + 1) << "Processing frame_id gap unexpected";
    }

    auto s = sensor->stats();
    ASSERT_GE(s.produced, s.emitted);
    ASSERT_EQ(s.emitted, (uint64_t)targetEmitted);
    if (s.produced >= 4) {
        uint64_t expectedDropsApprox = s.produced / 4; // dropEveryN=4
        EXPECT_NEAR((double)s.dropped, (double)expectedDropsApprox, 1.0);
        EXPECT_EQ(s.dropped, s.produced - s.emitted);
    }

    // Optional: gap stats should be zero since processing fills drops, continuity maintained
    auto stats = client.stats();
    EXPECT_EQ(stats.max_gap, 0u);
    harness.stop();
}
