#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>

#include "processing/ProcessingManager.h"
#include "hal/SyntheticSensorDevice.h"
#include "common/DataTypes.h"
#include "helpers/MemoryUtils.h"

using namespace caldera::backend::processing;
using namespace caldera::backend::hal;
using namespace caldera::backend::common;

class HALMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        baseline_memory_ = MemoryUtils::getCurrentRSS();
    }

    void TearDown() override {
        // Give system time to cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        size_t final_memory = MemoryUtils::getCurrentRSS();
        
        // Allow up to 5% memory growth from baseline
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(baseline_memory_, final_memory, 5.0))
            << "Memory grew from " << baseline_memory_ << " to " << final_memory 
            << " bytes (" << (((double)(final_memory - baseline_memory_) / baseline_memory_) * 100.0) << "% growth)";
    }

private:
    size_t baseline_memory_ = 0;
};

TEST_F(HALMemoryTest, ProcessingManagerLifecycle) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test ProcessingManager creation and destruction multiple times
    for (int i = 0; i < 10; ++i) {
        {
            // ProcessingManager requires logger - use null logger for tests
            auto processing_manager = std::make_unique<ProcessingManager>(nullptr, nullptr, 0.001f);
            
            // Allow manager to initialize
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } // ProcessingManager destructor should clean up here
        
        // Give system time to deallocate
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should not grow significantly after multiple create/destroy cycles
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 2.0))
        << "ProcessingManager lifecycle caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}

TEST_F(HALMemoryTest, SyntheticSensorMemoryStability) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test SyntheticSensorDevice with frame generation
    {
        SyntheticSensorDevice::Config config;
        config.width = 320;
        config.height = 240;
        config.fps = 30.0;
        config.pattern = SyntheticSensorDevice::Pattern::RAMP;
        
        auto synthetic_device = std::make_unique<SyntheticSensorDevice>(config, nullptr);
        
        EXPECT_FALSE(synthetic_device->isRunning());
        
        // Set up callback and start device
        synthetic_device->setFrameCallback([](const RawDepthFrame& depth_frame, const RawColorFrame& color_frame) {
            // Simple callback that doesn't retain frame data
            (void)depth_frame; // Suppress unused parameter warning
            (void)color_frame; // Suppress unused parameter warning
        });
        
        bool started = synthetic_device->open();
        EXPECT_TRUE(started);
        EXPECT_TRUE(synthetic_device->isRunning());
        
        size_t running_memory = MemoryUtils::getCurrentRSS();
        
        // Let it run for a short period to generate frames
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        size_t mid_run_memory = MemoryUtils::getCurrentRSS();
        
        // Memory should be stable during frame generation
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(running_memory, mid_run_memory, 10.0))
            << "SyntheticSensorDevice memory grew during operation from " 
            << running_memory << " to " << mid_run_memory << " bytes";
        
        // Stop the device
        synthetic_device->close();
        EXPECT_FALSE(synthetic_device->isRunning());
        
        // Give time for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    // Verify cleanup after device destruction
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 3.0))
        << "SyntheticSensorDevice lifecycle caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}

TEST_F(HALMemoryTest, SensorDeviceRapidCycling) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test rapid start/stop cycles to detect memory accumulation
    SyntheticSensorDevice::Config config;
    config.width = 160;
    config.height = 120;
    config.fps = 60.0;
    config.pattern = SyntheticSensorDevice::Pattern::CONSTANT;
    
    auto synthetic_device = std::make_unique<SyntheticSensorDevice>(config, nullptr);
    
    synthetic_device->setFrameCallback([](const RawDepthFrame& depth_frame, const RawColorFrame& color_frame) {
        (void)depth_frame;
        (void)color_frame;
    });
    
    for (int cycle = 0; cycle < 5; ++cycle) {
        // Start device
        bool started = synthetic_device->open();
        EXPECT_TRUE(started);
        
        // Run briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Stop device
        synthetic_device->close();
        EXPECT_FALSE(synthetic_device->isRunning());
        
        // Brief pause between cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    size_t post_cycling_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should not accumulate through start/stop cycles
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_cycling_memory, 5.0))
        << "Rapid sensor cycling caused memory growth from " 
        << pre_test_memory << " to " << post_cycling_memory << " bytes";
}

TEST_F(HALMemoryTest, BasicMemoryStability) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test simple memory stability with processing manager operations
    {
        auto processing_manager = std::make_unique<ProcessingManager>(nullptr, nullptr, 0.001f);
        
        // Simulate some processing operations without actual frame data
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should not grow significantly
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 5.0))
        << "Basic memory stability test caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}

TEST_F(HALMemoryTest, ExtendedProcessingRuntime) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test extended runtime to detect slow memory leaks in ProcessingManager
    auto processing_manager = std::make_unique<ProcessingManager>(nullptr, nullptr, 0.001f);
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    // Run for extended period with periodic memory checks
    const int check_intervals = 10;
    const int interval_ms = 200; // Total ~2 seconds
    
    std::vector<size_t> memory_samples;
    memory_samples.reserve(check_intervals);
    
    for (int i = 0; i < check_intervals; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        memory_samples.push_back(MemoryUtils::getCurrentRSS());
    }
    
    // Analyze memory trend
    size_t max_memory = *std::max_element(memory_samples.begin(), memory_samples.end());
    size_t min_memory = *std::min_element(memory_samples.begin(), memory_samples.end());
    
    // Memory should remain stable (within 10% variation)
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(min_memory, max_memory, 10.0))
        << "Extended runtime memory variation too high: " 
        << min_memory << " to " << max_memory << " bytes";
    
    // Final memory check after component destruction
    processing_manager.reset();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 5.0))
        << "Extended runtime test caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}

// Note: KinectV1_Device tests are commented out to avoid hardware dependencies
// in memory testing. Uncomment and modify if testing with actual hardware.
/*
TEST_F(HALMemoryTest, KinectV1DeviceMemoryBaseline) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test KinectV1_Device lifecycle (requires libfreenect)
    {
        auto kinect_device = std::make_unique<KinectV1_Device>(0);
        
        // Test connection attempt (may fail without hardware)
        bool started = kinect_device->start([](const RawDepthFrame& frame) {
            (void)frame;
        });
        
        if (started) {
            // If hardware available, test brief operation
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            kinect_device->stop();
        }
        
        // Memory test focuses on object lifecycle regardless of hardware
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 10.0))
        << "KinectV1_Device lifecycle caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}
*/