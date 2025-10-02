#include <gtest/gtest.h>
#include "hal/KinectV1_Device.h"
#include "common/Logger.h"

// Physical device open/close test for Kinect v1 (mirrors KinectV2_DeviceTest.PhysicalDeviceOpenClose naming).
// Skips if hardware or build support absent. Set CALDERA_REQUIRE_KINECT_V1=1 to force failure instead of skip.

TEST(KinectV1_DeviceTest, PhysicalDeviceOpenClose) {
#if !CALDERA_HAVE_KINECT_V1
    GTEST_SKIP() << "Built without Kinect v1 support (CALDERA_HAVE_KINECT_V1=0)";
#else
    using caldera::backend::common::Logger;
    auto &L = Logger::instance(); if (!L.isInitialized()) L.initialize("logs/test/kinectv1_smoke.log");
    caldera::backend::hal::KinectV1_Device dev;
    // If env CALDERA_REQUIRE_KINECT_V1 == 1, we assert success, otherwise skip if open fails.
    bool require = false; if (const char* e = std::getenv("CALDERA_REQUIRE_KINECT_V1")) require = (std::string(e) == "1");
    if (!dev.open()) {
        if (require) {
            FAIL() << "Kinect v1 open() failed but CALDERA_REQUIRE_KINECT_V1=1";
        } else {
            GTEST_SKIP() << "Kinect v1 not available (open() failed); skipping hardware test";
        }
    } else {
        EXPECT_TRUE(dev.isRunning());
        dev.close();
        EXPECT_FALSE(dev.isRunning());
    }
#endif
}
