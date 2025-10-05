#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "processing/TemporalFilter.h"
#include "common/Logger.h"

using namespace caldera::backend::processing;
using namespace caldera::backend::common;

class ProcessingInvalidPixelsTest : public ::testing::Test {
protected:
    std::shared_ptr<spdlog::logger> orchLogger;
    std::shared_ptr<spdlog::logger> fusionLogger;

    void SetUp() override {
        if (!Logger::instance().isInitialized()) {
            Logger::instance().initialize("logs/test/processing_invalid_pixels.log");
        }
        orchLogger = Logger::instance().get("Processing.Orchestrator");
        fusionLogger = Logger::instance().get("Processing.Fusion");
    }
};

TEST_F(ProcessingInvalidPixelsTest, BasicInvalidAndValidCounting) {
    ProcessingManager mgr(orchLogger, fusionLogger, 0.001f);

    // Attach a simple temporal filter to exercise pipeline (small window)
    auto tf = std::make_shared<TemporalFilter>();
    mgr.setHeightMapFilter(tf);

    RawDepthFrame raw;
    raw.sensorId = "kinect-v1";
    raw.width = 4;
    raw.height = 4;
    raw.timestamp_ns = 123456789ULL;
    raw.data.resize(16);

    // Layout:
    // Row 0: all zeros (invalid)
    // Row 1: valid shallow depths
    // Row 2: mix (some zeros)
    // Row 3: larger depths still within range
    raw.data = {
        0,0,0,0,
        800,810,820,830,
        0,900,0,910,
        1200,1300,1400,1500
    };

    WorldFrame received; bool got=false;
    mgr.setWorldFrameCallback([&](const WorldFrame& wf){ received = wf; got=true; });

    mgr.processRawDepthFrame(raw);

    ASSERT_TRUE(got);
    ASSERT_EQ(received.heightMap.width, 4);
    ASSERT_EQ(received.heightMap.height, 4);

    auto summary = mgr.lastValidationSummary();
    // invalid: row0 (4) + row2 zeros (2) = 6
    EXPECT_EQ(summary.invalid, 6u);
    EXPECT_EQ(summary.valid, 10u);

    // Check that invalid pixels got replaced by 0 after filtering stage assembly
    // Count zeros
    size_t zeroCount = 0;
    for (float v : received.heightMap.data) {
        if (v == 0.0f) zeroCount++;
    }
    EXPECT_GE(zeroCount, 6u); // At least invalid set -> 0 (temporal filter may keep 0 for first frame)
}
