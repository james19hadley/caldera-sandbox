#include <gtest/gtest.h>
#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "tools/calibration/SensorCalibration.h"
#include <fstream>
#include <cstdlib>

using namespace caldera::backend;
using namespace caldera::backend::processing;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;
using caldera::backend::tools::calibration::SensorCalibration;
using caldera::backend::tools::calibration::SensorCalibrationProfile;
using caldera::backend::tools::calibration::PlaneEquation;

// Helper to write a minimal calibration profile file through the existing SensorCalibration API.
static SensorCalibrationProfile makeProfile(const std::string& sensorId,
                                            float minPlaneD,
                                            float maxPlaneD) {
    SensorCalibrationProfile p;
    p.sensorId = sensorId;
    p.sensorType = "kinect-v1";
    p.basePlaneCalibration.basePlane = {0,0,1,0};
    // define min (below = invalid) plane at d=minPlaneD and max at d=maxPlaneD
    p.minValidPlane = {0,0,1,minPlaneD};
    p.maxValidPlane = {0,0,1,maxPlaneD};
    return p;
}

TEST(CalibrationProfileLoadingTest, AppliesProfilePlanesOverFallback) {
    // Create a temp calibration directory
    std::string calibDir = "test_calib_dir";
    std::filesystem::create_directory(calibDir);

    const std::string sensorId = "test-sensor-profile";
    // Create profile with a narrow valid band (0.2m to 0.6m elevation)
    auto profile = makeProfile(sensorId, -0.2f, -0.6f); // Using plane form a*x+b*y+c*z+d=0 => z = -d when a=b=0,c=1

    // Ensure logger system initialized (some components expect it)
    // Initialize logger (single argument: log file path). Other params default.
    caldera::backend::common::Logger::instance().initialize("logs/test_profile_loading.log");
    SensorCalibration calib;
    calib.setCalibrationDirectory(calibDir);
    ASSERT_TRUE(calib.saveCalibrationProfile(profile));

    // Create a raw frame with depths such that only a subset falls inside the 0.2..0.6m band given scale 0.001
    RawDepthFrame raw;
    raw.sensorId = sensorId;
    raw.width = 5;
    raw.height = 1;
    raw.data = {100, 150, 250, 700, 1200}; // 0.1,0.15,0.25,0.7,1.2 m

    // Setup env so processing loads the profile and also set fallback planes that should be overridden.
    setenv("CALDERA_CALIB_SENSOR_ID", sensorId.c_str(), 1);
    setenv("CALDERA_CALIB_DIR", calibDir.c_str(), 1);
    setenv("CALDERA_CALIB_MIN_PLANE", "0,0,1,-0.5", 1); // would allow 0.5m+ if used
    setenv("CALDERA_CALIB_MAX_PLANE", "0,0,1,-2.0", 1); // wide upper bound

    auto logger = spdlog::default_logger();
    ProcessingManager pm(logger);

    WorldFrame captured;
    pm.setWorldFrameCallback([&](const WorldFrame& wf){ captured = wf; });

    pm.processRawDepthFrame(raw);

    auto summary = pm.lastValidationSummary();
    // With profile narrow band (0.2..0.6m), only depth=250 (0.25m) should be valid.
    EXPECT_EQ(summary.valid, 1u);
    EXPECT_EQ(summary.invalid, 4u);

    // Cleanup
    std::filesystem::remove_all(calibDir);
    unsetenv("CALDERA_CALIB_SENSOR_ID");
    unsetenv("CALDERA_CALIB_DIR");
    unsetenv("CALDERA_CALIB_MIN_PLANE");
    unsetenv("CALDERA_CALIB_MAX_PLANE");
}
