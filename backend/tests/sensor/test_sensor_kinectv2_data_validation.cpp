#include <gtest/gtest.h>
#include "hal/KinectV2_Device.h"
#include "common/DataTypes.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <memory>

class KinectDataValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device = std::make_unique<caldera::backend::hal::KinectV2_Device>();
    }

    void TearDown() override {
        if (device->isRunning()) {
            device->close();
        }
        device.reset();
    }

    std::unique_ptr<caldera::backend::hal::KinectV2_Device> device;
    std::atomic<int> frame_count{0};
    std::atomic<bool> received_depth{false};
    std::atomic<bool> received_color{false};
    caldera::backend::common::RawDepthFrame last_depth_frame;
    caldera::backend::common::RawColorFrame last_color_frame;
    std::mutex frame_mutex;
};

TEST_F(KinectDataValidationTest, DISABLED_FrameDataValidation) {
    // Disabled by default - requires physical device
    
    if (!device->open()) {
        GTEST_SKIP() << "No Kinect V2 device available";
    }

    device->setFrameCallback([this](const caldera::backend::common::RawDepthFrame& depth, const caldera::backend::common::RawColorFrame& color) {
        std::lock_guard<std::mutex> lock(frame_mutex);
        
        // Validate depth frame
        EXPECT_GT(depth.width, 0);
        EXPECT_GT(depth.height, 0);
        EXPECT_EQ(depth.data.size(), depth.width * depth.height);
        EXPECT_GT(depth.timestamp, 0);
        
        // Validate color frame  
        EXPECT_GT(color.width, 0);
        EXPECT_GT(color.height, 0);
        EXPECT_GT(color.data.size(), 0);
        EXPECT_GT(color.timestamp, 0);
        
        // Check reasonable dimensions (Kinect V2 typical values)
        EXPECT_GE(depth.width, 480);   // Should be 512
        EXPECT_GE(depth.height, 424);  // Should be 424
        EXPECT_GE(color.width, 1900);  // Should be 1920
        EXPECT_GE(color.height, 1080); // Should be 1080
        
        last_depth_frame = depth;
        last_color_frame = color;
        
        received_depth.store(true);
        received_color.store(true);
        frame_count.fetch_add(1);
    });

    // Wait for some frames
    auto start_time = std::chrono::steady_clock::now();
    while (frame_count.load() < 10 && 
           std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(received_depth.load());
    EXPECT_TRUE(received_color.load());
    EXPECT_GE(frame_count.load(), 5); // Should get at least 5 frames in 5 seconds
}

TEST_F(KinectDataValidationTest, DISABLED_DepthDataRangeValidation) {
    // Disabled by default - requires physical device
    
    if (!device->open()) {
        GTEST_SKIP() << "No Kinect V2 device available";
    }

    std::atomic<bool> got_frame{false};
    
    device->setFrameCallback([&](const caldera::backend::common::RawDepthFrame& depth, const caldera::backend::common::RawColorFrame& color) {
        std::lock_guard<std::mutex> lock(frame_mutex);
        
        // Check depth value ranges (Kinect V2: 0-8000mm typically)
        int valid_pixels = 0;
        int zero_pixels = 0;
        int max_depth = 0;
        
        for (size_t i = 0; i < depth.data.size(); i++) {
            uint16_t depth_mm = depth.data[i];
            
            if (depth_mm == 0) {
                zero_pixels++;
            } else if (depth_mm > 0 && depth_mm < 10000) { // Reasonable range
                valid_pixels++;
                max_depth = std::max(max_depth, (int)depth_mm);
            }
        }
        
        // Should have some valid depth pixels
        EXPECT_GT(valid_pixels, 1000); // At least some valid data
        EXPECT_GT(max_depth, 500);     // Should see something beyond 50cm
        EXPECT_LT(max_depth, 10000);   // But not beyond 10m
        
        got_frame.store(true);
    });

    // Wait for one good frame
    auto start_time = std::chrono::steady_clock::now();
    while (!got_frame.load() && 
           std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(got_frame.load());
}