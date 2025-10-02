// Integration: Processing scale semantics validation (formerly Phase 1)
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "IntegrationHarness.h"
#include "transport/SharedMemoryReader.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::transport::SharedMemoryReader;

static void regenerateRamp(int w, int h, std::vector<uint16_t>& out) {
    out.resize(static_cast<size_t>(w) * h);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) out[static_cast<size_t>(y)*w + x] = static_cast<uint16_t>(x + y);
}

// Collect one new frame with timeout
static std::optional<caldera::backend::transport::SharedMemoryReader::FrameView> waitFrame(SharedMemoryReader& r, uint64_t& lastId, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto opt = r.latest();
        if (opt && opt->frame_id != lastId) { lastId = opt->frame_id; return opt; }
        std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
}

static void runScaleCase(float scale) {
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.width=8; sc.height=8; sc.fps=40.0; sc.pattern=SyntheticSensorDevice::Pattern::RAMP; sc.sensorId="ScaleSynth";
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name = "/caldera_integration_scale"; hc.max_width=16; hc.max_height=16; hc.processing_scale=scale;
    ASSERT_TRUE(harness.start(hc));

    SharedMemoryReader reader(caldera::backend::common::Logger::instance().get("Integration.ScaleReader"));
    ASSERT_TRUE(reader.open(hc.shm_name, hc.max_width, hc.max_height));

    std::vector<uint16_t> pattern; regenerateRamp(sc.width, sc.height, pattern);
    std::vector<float> expected(pattern.size());
    for (size_t i=0;i<pattern.size();++i) expected[i] = static_cast<float>(pattern[i]) * scale;

    uint64_t lastId = UINT64_MAX;
    int okFrames = 0;
    while (okFrames < 5) {
        auto f = waitFrame(reader, lastId, 1500ms);
        ASSERT_TRUE(f.has_value()) << "Timed out waiting for frame";
        ASSERT_EQ(f->width, sc.width);
        ASSERT_EQ(f->height, sc.height);
        ASSERT_EQ(f->float_count, expected.size());
        const float* data = f->data;
        for (size_t i=0;i<expected.size();++i) {
            ASSERT_NEAR(data[i], expected[i], 1e-6f) << "Mismatch at idx=" << i;
        }
        ++okFrames;
    }
    harness.stop();
}

TEST(ProcessingScaleSemantics, Scale001) { runScaleCase(0.001f); }
TEST(ProcessingScaleSemantics, Scale010) { runScaleCase(0.010f); }
