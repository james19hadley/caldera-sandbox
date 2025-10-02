#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <unordered_map>

#include "IntegrationHarness.h"
#include "transport/SharedMemoryReader.h"
#include "common/Checksum.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::transport::SharedMemoryReader;
using caldera::backend::common::crc32;

// Helper: regenerate ramp pattern (uint16_t -> then scaled by ProcessingManager scale during pipeline).
static void regenerateRamp(int w, int h, std::vector<uint16_t>& out) {
    out.resize(static_cast<size_t>(w) * h);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) out[static_cast<size_t>(y)*w + x] = static_cast<uint16_t>(x + y);
}

// Compute CRC over float buffer bytes (stable representation assumption: IEEE 754, little-endian typical CI/Linux).
static uint32_t crcFloats(const std::vector<float>& data) { return crc32(data); }

TEST(IntegrationPhase0, SingleSyntheticSensorPassThrough) {
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.width=16; sc.height=16; sc.fps=30.0; sc.pattern=SyntheticSensorDevice::Pattern::RAMP; sc.sensorId="SynthA";
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name = "/caldera_integration_phase0"; hc.max_width=32; hc.max_height=32;
    ASSERT_TRUE(harness.start(hc));

    SharedMemoryReader reader(caldera::backend::common::Logger::instance().get("Integration.Reader"));
    ASSERT_TRUE(reader.open(hc.shm_name, hc.max_width, hc.max_height));

    const int targetFrames = 10;
    int collected = 0;
    uint64_t lastFrameId = UINT64_MAX;
    std::vector<uint16_t> expectedPattern;
    regenerateRamp(sc.width, sc.height, expectedPattern);
    // Precompute expected depth->height scaling: ProcessingManager default scale is 0.001 (unless env overrides)
    const float scale = 0.001f;
    std::vector<float> expectedScaled(expectedPattern.size());
    for (size_t i=0;i<expectedPattern.size();++i) expectedScaled[i] = static_cast<float>(expectedPattern[i]) * scale;
    uint32_t expectedCRC = crcFloats(expectedScaled);

    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (collected < targetFrames && std::chrono::steady_clock::now() < deadline) {
        auto opt = reader.latest();
        if (opt && opt->frame_id != lastFrameId) {
            lastFrameId = opt->frame_id;
            // Basic assertions
            ASSERT_EQ(opt->width, sc.width);
            ASSERT_EQ(opt->height, sc.height);
            ASSERT_EQ(opt->float_count, expectedScaled.size());
            // Compute CRC of received floats
            uint32_t gotCRC = crc32(opt->data, opt->float_count);
            EXPECT_EQ(gotCRC, expectedCRC) << "CRC mismatch at frame_id=" << opt->frame_id;
            ++collected;
        }
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(collected, targetFrames) << "Did not collect enough frames";
    EXPECT_GE(harness.framesPublished(), static_cast<uint64_t>(targetFrames));

    harness.stop();
}
