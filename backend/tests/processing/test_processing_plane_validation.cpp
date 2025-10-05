#include "processing/ProcessingManager.h"
#include "processing/ProcessingTypes.h"
#include "common/DataTypes.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

namespace {
std::shared_ptr<spdlog::logger> makeLogger() {
    auto logger = spdlog::default_logger();
    logger->set_level(spdlog::level::off);
    return logger;
}
}

TEST(ProcessingPlaneValidationTest, RejectsBelowMinPlaneAndAboveMaxPlane) {
    auto logger = makeLogger();
    ProcessingManager mgr(logger);

    // Construct transform parameters with min plane z >= 0.5 and max plane z <= 2.0 (already defaults)
    TransformParameters params; // defaults: min z 0.5, max z 2.0 via d terms
    params.minValidPlane = {0.f,0.f,1.f,-0.5f}; // z >= 0.5
    params.maxValidPlane = {0.f,0.f,1.f,-2.0f}; // z <= 2.0
    mgr.setTransformParameters(params);

    RawDepthFrame raw;
    raw.width = 4;
    raw.height = 1;
    raw.sensorId = "test";
    raw.timestamp_ns = 123;
    raw.data.resize(4);
    // depthScale default is 0.001 (mm->m). So to get z meters, use mm value.
    // Values: 0.4m (invalid below min), 0.5m (valid boundary), 1.0m (valid), 2.5m (invalid above max)
    raw.data[0] = 400; // 0.4m
    raw.data[1] = 500; // 0.5m
    raw.data[2] = 1000; //1.0m
    raw.data[3] = 2500; //2.5m

    int callbackCount = 0;
    caldera::backend::common::WorldFrame captured;
    mgr.setWorldFrameCallback([&](const caldera::backend::common::WorldFrame& wf){ captured = wf; ++callbackCount; });

    mgr.processRawDepthFrame(raw);

    ASSERT_EQ(callbackCount, 1);
    // Expect 2 valid points (0.5,1.0) and 2 invalid (0.4,2.5)
    const auto& summary = mgr.lastValidationSummary();
    EXPECT_EQ(summary.valid, 2u);
    EXPECT_EQ(summary.invalid, 2u);

    ASSERT_EQ(captured.heightMap.data.size(), 4u);
    // Invalid positions replaced with 0.0f in output height map.
    EXPECT_FLOAT_EQ(captured.heightMap.data[0], 0.0f);
    EXPECT_FLOAT_EQ(captured.heightMap.data[1], 0.5f);
    EXPECT_FLOAT_EQ(captured.heightMap.data[2], 1.0f);
    EXPECT_FLOAT_EQ(captured.heightMap.data[3], 0.0f);
}
