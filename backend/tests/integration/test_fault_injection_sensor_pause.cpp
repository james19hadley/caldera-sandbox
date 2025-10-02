// Phase 6: Fault Injection - Sensor pause / resume (drop / stutter simulation)
// Validates pipeline stability when a sensor temporarily stops producing frames
// and resumes later without frame_id reset.

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <optional>
#include <limits>

#include "IntegrationHarness.h"
#include "helpers/TestCalderaClient.h"

using namespace std::chrono_literals;
using caldera::backend::hal::SyntheticSensorDevice;

TEST(FaultInjection, SensorPauseResumeBasic) {
    caldera::backend::tests::IntegrationHarness harness;
    SyntheticSensorDevice::Config scfg;
    scfg.width = 16; scfg.height = 16; scfg.fps = 30.0; scfg.pattern = SyntheticSensorDevice::Pattern::RAMP;
    scfg.sensorId = "FaultSensor";
    harness.addSyntheticSensor(scfg);
    caldera::backend::tests::HarnessConfig hcfg; hcfg.shm_name = "/caldera_integration_fault"; hcfg.max_width=32; hcfg.max_height=32; hcfg.processing_scale=0.001f;
    ASSERT_TRUE(harness.start(hcfg));

    auto logger = caldera::backend::common::Logger::instance().get("Test.FaultInjection");
    TestCalderaClient client(logger);
    ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{hcfg.shm_name, static_cast<uint32_t>(hcfg.max_width), static_cast<uint32_t>(hcfg.max_height), true, 2000}));

    auto* sensor = harness.syntheticSensor(0);
    ASSERT_NE(sensor, nullptr);

    uint64_t last_frame_id = std::numeric_limits<uint64_t>::max();
    int received = 0;
    auto start = std::chrono::steady_clock::now();
    while (received < 5 && std::chrono::steady_clock::now() - start < 2s) {
        if (auto fv = client.latest()) {
            if (fv->frame_id != last_frame_id) {
                last_frame_id = fv->frame_id;
                ++received;
            }
        }
        std::this_thread::sleep_for(2ms);
    }
    ASSERT_EQ(received, 5) << "Did not receive initial 5 frames before pause";
    uint64_t paused_id = last_frame_id;
    // Manually pause now
    sensor->pause();
    // Verify pause holds
    bool advanced = false; auto pause_check_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - pause_check_start < 200ms) {
        if (auto fv = client.latest(); fv && fv->frame_id != paused_id) { advanced = true; break; }
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_FALSE(advanced) << "Frame id advanced while sensor manually paused";
    EXPECT_TRUE(sensor->isPaused());

    // Resume and ensure frames continue monotonically
    sensor->resume();
    int post = 0;
    uint64_t last_after = paused_id;
    auto resume_deadline = std::chrono::steady_clock::now() + 2s;
    while (post < 5 && std::chrono::steady_clock::now() < resume_deadline) {
        if (auto fv = client.latest()) {
            if (fv->frame_id != last_after) {
                EXPECT_GT(fv->frame_id, last_after);
                last_after = fv->frame_id;
                ++post;
            }
        }
        std::this_thread::sleep_for(2ms);
    }
    EXPECT_EQ(post, 5) << "Did not receive 5 frames after resume";
    // Gap stats sanity: during pause we expect no gaps reported (no frames advancing), after resume continuity should hold.
    auto s = client.stats();
    EXPECT_EQ(s.checksum_mismatch, 0u);
    harness.stop();
}