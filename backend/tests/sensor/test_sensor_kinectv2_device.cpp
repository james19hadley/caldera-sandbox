#include <gtest/gtest.h>
#include "common/Logger.h"
#include "hal/KinectV2_Device.h"

using caldera::backend::common::Logger;
using caldera::backend::hal::KinectV2_Device;

class KinectV2_DeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!Logger::instance().isInitialized()) {
            Logger::instance().initialize("logs/test/kinectv2_device.log");
            Logger::instance().setGlobalLevel(spdlog::level::debug);
        }
    }
};

TEST_F(KinectV2_DeviceTest, ConstructorCreatesValidDevice) {
    KinectV2_Device device;
    
    // Device should not be running initially
    EXPECT_FALSE(device.isRunning());
    
    // Device ID should be empty until opened
    EXPECT_TRUE(device.getDeviceID().empty());
}

TEST_F(KinectV2_DeviceTest, CallbackCanBeSet) {
    KinectV2_Device device;
    bool callbackCalled = false;
    
    device.setFrameCallback([&callbackCalled](const auto& depth, const auto& color) {
        callbackCalled = true;
    });
    
    // Callback is set (we can't easily test this without opening the device)
    // This test mainly ensures the method doesn't crash
}

// This test requires a physical Kinect V2 connected via USB
TEST_F(KinectV2_DeviceTest, PhysicalDeviceOpenClose) {
    KinectV2_Device device;
    
    // Try to open - this will fail if no physical device is connected
    bool opened = device.open();
    
    if (opened) {
        EXPECT_TRUE(device.isRunning());
        EXPECT_FALSE(device.getDeviceID().empty());
        
        // Close the device
        device.close();
        EXPECT_FALSE(device.isRunning());
    } else {
        GTEST_SKIP() << "No physical Kinect V2 device detected";
    }
}