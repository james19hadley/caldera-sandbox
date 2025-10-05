#include "processing/ProcessingManager.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <spdlog/spdlog.h>

using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;

TEST(EnvCalibrationFallbackTest, AppliesEnvPlanes) {
    // Set env planes so that only depths >= 1.0m and <= 1.5m are valid (z measured directly as depth*scale)
    // plane form: a,b,c,d with c=1; condition: minPlane(x,y,z)>=0 -> a*x+b*y+z+d>=0 => z >= -d; choose d=-1.0 (z>=1.0)
    // maxPlane: z <= 1.5 -> a=0,b=0,c=1,d=-1.5 and we require maxPlane(x,y,z)<=0 -> z-1.5<=0
    setenv("CALDERA_CALIB_MIN_PLANE", "0,0,1,-1.0", 1);
    setenv("CALDERA_CALIB_MAX_PLANE", "0,0,1,-1.5", 1);

    auto logger = spdlog::default_logger();
    ProcessingManager mgr(logger);

    RawDepthFrame raw; raw.width=3; raw.height=1; raw.sensorId="envTest"; raw.timestamp_ns=42; raw.data = {900, 1000, 1600};
    // scale default 0.001 -> depths: 0.9m (invalid), 1.0m (valid), 1.6m (invalid)
    mgr.setWorldFrameCallback([](const caldera::backend::common::WorldFrame&){ });
    mgr.processRawDepthFrame(raw);

    const auto& summary = mgr.lastValidationSummary();
    EXPECT_EQ(summary.valid, 1u);
    EXPECT_EQ(summary.invalid, 2u);
}
