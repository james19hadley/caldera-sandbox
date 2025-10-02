#include <gtest/gtest.h>
#include "hal/KinectV1_Device.h"
#include "common/Logger.h"

// Minimal smoke test for Kinect v1 (libfreenect). This test is hardware-gated and will be skipped
// if the build lacks Kinect v1 support or the device is not present.

TEST(KinectV1_DeviceTest, OpenCloseSmoke) {
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
