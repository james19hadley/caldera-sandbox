// Phase 1: HAL→Processing Memory Integration Tests
// Tests memory behavior when SyntheticSensorDevice feeds data to ProcessingManager
// Focus: Memory stability during frame processing callbacks and data flow

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>

#include "hal/SyntheticSensorDevice.h"
#include "processing/ProcessingManager.h"
#include "common/Logger.h"
#include "helpers/MemoryUtils.h"

using namespace std::chrono_literals;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::RawColorFrame;
using caldera::backend::common::WorldFrame;

class HALProcessingMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!caldera::backend::common::Logger::instance().isInitialized()) {
            caldera::backend::common::Logger::instance().initialize("logs/test/hal_processing_memory.log");
        }
        baseline_memory_ = MemoryUtils::getCurrentRSS();
    }

    void TearDown() override {
        size_t final_memory = MemoryUtils::getCurrentRSS();
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(baseline_memory_, final_memory, 5.0))
            << "Memory grew from " << baseline_memory_ << " to " << final_memory << " bytes ("
            << MemoryUtils::calculateGrowthPercent(baseline_memory_, final_memory) << "% growth)";
    }

    std::shared_ptr<spdlog::logger> logger(const std::string& name) {
        return caldera::backend::common::Logger::instance().get(name);
    }

private:
    size_t baseline_memory_;
};

// Test 1: Basic HAL→Processing pipeline memory stability
TEST_F(HALProcessingMemoryTest, BasicPipelineMemoryStability) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Create processing manager
    std::atomic<uint32_t> frames_processed{0};
    auto processing_manager = std::make_unique<ProcessingManager>(
        logger("HALProcessingTest.Processing"), 
        nullptr, // fusion logger
        0.001f // minimal processing scale
    );
    
    // Set callback for processed world frames
    processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
        frames_processed.fetch_add(1);
    });
    
    // Create synthetic sensor
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 64;
    sensor_config.height = 64; 
    sensor_config.fps = 30.0;
    sensor_config.pattern = SyntheticSensorDevice::Pattern::RAMP;
    sensor_config.sensorId = "HALProcTest1";
    
    auto sensor = std::make_unique<SyntheticSensorDevice>(sensor_config, logger("HALProcessingTest.Sensor"));
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    // Start components
    ASSERT_TRUE(sensor->open());
    
    // Connect sensor to processing
    sensor->setFrameCallback([&](const RawDepthFrame& depth_frame, const RawColorFrame& color_frame) {
        processing_manager->processRawDepthFrame(depth_frame);
    });
    
    // Run for a period to generate frame traffic
    std::this_thread::sleep_for(1s);
    
    size_t mid_run_memory = MemoryUtils::getCurrentRSS();
    
    // Continue running to check for memory growth
    std::this_thread::sleep_for(1s);
    
    size_t end_memory = MemoryUtils::getCurrentRSS();
    
    // Stop components
    sensor->close();
    processing_manager.reset();
    sensor.reset();
    
    // Allow cleanup time
    std::this_thread::sleep_for(100ms);
    
    size_t post_cleanup_memory = MemoryUtils::getCurrentRSS();
    
    // Validate frame processing occurred
    EXPECT_GT(frames_processed.load(), 50u) << "Expected significant frame processing";
    
    // Memory should remain stable during operation
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(start_memory, mid_run_memory, 15.0))
        << "Memory grew during first second from " << start_memory << " to " << mid_run_memory << " bytes";
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(mid_run_memory, end_memory, 10.0))
        << "Memory grew during second second from " << mid_run_memory << " to " << end_memory << " bytes";
    
    // Overall test should not cause significant memory growth
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_cleanup_memory, 8.0))
        << "HAL→Processing test caused memory growth from " << pre_test_memory 
        << " to " << post_cleanup_memory << " bytes";
}

// Test 2: High-frequency frame processing memory behavior
TEST_F(HALProcessingMemoryTest, HighFrequencyProcessingMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    std::atomic<uint32_t> frames_processed{0};
    auto processing_manager = std::make_unique<ProcessingManager>(
        logger("HALProcessingTest.HighFreq.Processing"),
        nullptr, // fusion logger
        0.01f // higher processing scale for more work
    );
    
    processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
        frames_processed.fetch_add(1);
    });
    
    // High FPS sensor configuration
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 128;
    sensor_config.height = 128;
    sensor_config.fps = 120.0; // High frequency
    sensor_config.pattern = SyntheticSensorDevice::Pattern::CHECKER;
    sensor_config.sensorId = "HALProcHighFreq";
    
    auto sensor = std::make_unique<SyntheticSensorDevice>(sensor_config, logger("HALProcessingTest.HighFreq.Sensor"));
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    // Start and connect
    ASSERT_TRUE(sensor->open());
    sensor->setFrameCallback([&](const RawDepthFrame& depth_frame, const RawColorFrame& color_frame) {
        processing_manager->processRawDepthFrame(depth_frame);
    });
    
    // Collect memory samples during high-frequency operation
    std::vector<size_t> memory_samples;
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(200ms);
        memory_samples.push_back(MemoryUtils::getCurrentRSS());
    }
    
    sensor->close();
    processing_manager.reset();
    sensor.reset();
    
    std::this_thread::sleep_for(100ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Validate high frame rate achieved
    EXPECT_GT(frames_processed.load(), 100u) << "Expected high frame processing rate";
    
    // Memory should not show significant growth trend during high-frequency operation
    for (size_t i = 1; i < memory_samples.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(memory_samples[0], memory_samples[i], 12.0))
            << "Memory growth detected from sample 0 (" << memory_samples[0] 
            << ") to sample " << i << " (" << memory_samples[i] << ") bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 10.0))
        << "High-frequency test caused overall memory growth from " << pre_test_memory 
        << " to " << final_memory << " bytes";
}

// Test 3: Callback lifecycle memory management
TEST_F(HALProcessingMemoryTest, CallbackLifecycleMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test multiple connect/disconnect cycles
    for (int cycle = 0; cycle < 3; ++cycle) {
        std::atomic<uint32_t> frames_in_cycle{0};
        auto processing_manager = std::make_unique<ProcessingManager>(
            logger("HALProcessingTest.Lifecycle.Processing"),
            nullptr, // fusion logger
            0.001f
        );
        
        processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
            frames_in_cycle.fetch_add(1);
        });
        
        SyntheticSensorDevice::Config sensor_config;
        sensor_config.width = 32;
        sensor_config.height = 32;
        sensor_config.fps = 60.0;
        sensor_config.pattern = SyntheticSensorDevice::Pattern::RADIAL;
        sensor_config.sensorId = "HALProcCycle" + std::to_string(cycle);
        
        auto sensor = std::make_unique<SyntheticSensorDevice>(sensor_config, 
            logger("HALProcessingTest.Lifecycle.Sensor." + std::to_string(cycle)));
        
        // Connect and run briefly
        ASSERT_TRUE(sensor->open());
        sensor->setFrameCallback([&](const RawDepthFrame& depth_frame, const RawColorFrame& color_frame) {
            processing_manager->processRawDepthFrame(depth_frame);
        });
        
        std::this_thread::sleep_for(300ms);
        
        // Disconnect and cleanup
        sensor->close();
        sensor.reset();
        processing_manager.reset();
        
        std::this_thread::sleep_for(50ms);
        
        EXPECT_GT(frames_in_cycle.load(), 15u) << "Cycle " << cycle << " should process frames";
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Multiple cycles should not accumulate memory
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 8.0))
        << "Multiple callback lifecycle cycles caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}

// Test 4: Processing manager scaling memory impact
TEST_F(HALProcessingMemoryTest, ProcessingScaleMemoryImpact) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test different processing scales and their memory impact
    std::vector<float> scales = {0.001f, 0.1f, 0.5f, 1.0f};
    std::vector<size_t> memory_after_scale;
    
    for (float scale : scales) {
        std::atomic<uint32_t> frames_processed{0};
        auto processing_manager = std::make_unique<ProcessingManager>(
            logger("HALProcessingTest.Scale.Processing"),
            nullptr, // fusion logger
            scale
        );
        
        processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
            frames_processed.fetch_add(1);
        });
        
        SyntheticSensorDevice::Config sensor_config;
        sensor_config.width = 64;
        sensor_config.height = 64;
        sensor_config.fps = 30.0;
        sensor_config.pattern = SyntheticSensorDevice::Pattern::STRIPES;
        sensor_config.sensorId = "HALProcScale" + std::to_string((int)(scale * 1000));
        
        auto sensor = std::make_unique<SyntheticSensorDevice>(sensor_config, 
            logger("HALProcessingTest.Scale.Sensor"));
        
        ASSERT_TRUE(sensor->open());
        sensor->setFrameCallback([&](const RawDepthFrame& depth_frame, const RawColorFrame& color_frame) {
            processing_manager->processRawDepthFrame(depth_frame);
        });
        
        std::this_thread::sleep_for(500ms);
        
        sensor->close();
        sensor.reset();
        processing_manager.reset();
        
        std::this_thread::sleep_for(100ms);
        
        memory_after_scale.push_back(MemoryUtils::getCurrentRSS());
        
        EXPECT_GT(frames_processed.load(), 10u) << "Scale " << scale << " should process frames";
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory usage should not grow dramatically with processing scale
    for (size_t i = 0; i < memory_after_scale.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, memory_after_scale[i], 15.0))
            << "Processing scale " << scales[i] << " caused excessive memory growth to " 
            << memory_after_scale[i] << " bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 12.0))
        << "Overall processing scale test caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}