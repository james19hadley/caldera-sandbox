// Phase 1: Full Pipeline Memory Integration Tests  
// Tests end-to-end memory stability: HAL→Processing→Transport→Client using IntegrationHarness
// Focus: Memory stability during complete data flow through all components

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>

#include "IntegrationHarness.h"
#include "common/Logger.h"
#include "helpers/MemoryUtils.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;

class FullPipelineMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!caldera::backend::common::Logger::instance().isInitialized()) {
            caldera::backend::common::Logger::instance().initialize("logs/test/full_pipeline_memory.log");
        }
        baseline_memory_ = MemoryUtils::getCurrentRSS();
    }

    void TearDown() override {
        size_t final_memory = MemoryUtils::getCurrentRSS();
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(baseline_memory_, final_memory, 10.0))
            << "Memory grew from " << baseline_memory_ << " to " << final_memory << " bytes ("
            << MemoryUtils::calculateGrowthPercent(baseline_memory_, final_memory) << "% growth)";
    }

    std::shared_ptr<spdlog::logger> logger(const std::string& name) {
        return caldera::backend::common::Logger::instance().get(name);
    }

private:
    size_t baseline_memory_;
};

// Test 1: Basic full pipeline memory stability
TEST_F(FullPipelineMemoryTest, BasicFullPipelineMemoryStability) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    IntegrationHarness harness;
    
    // Configure synthetic sensor
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 64;
    sensor_config.height = 64; 
    sensor_config.fps = 30.0;
    sensor_config.pattern = SyntheticSensorDevice::Pattern::RAMP;
    sensor_config.sensorId = "FullPipelineTest1";
    
    harness.addSyntheticSensor(sensor_config);
    
    // Configure harness for pipeline test
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_full_pipeline_test_1";
    harness_config.max_width = 128;
    harness_config.max_height = 128;
    harness_config.processing_scale = 0.001f; // Minimal processing
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    size_t pipeline_active_memory = MemoryUtils::getCurrentRSS();
    
    // Let pipeline run to generate significant frame traffic
    std::this_thread::sleep_for(2s);
    
    size_t mid_run_memory = MemoryUtils::getCurrentRSS();
    
    // Continue running to check for memory growth
    std::this_thread::sleep_for(2s);
    
    size_t end_run_memory = MemoryUtils::getCurrentRSS();
    
    // Stop components
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t post_cleanup_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should remain stable during operation
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pipeline_active_memory, mid_run_memory, 20.0))
        << "Memory grew during first period from " << pipeline_active_memory << " to " << mid_run_memory << " bytes";
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(mid_run_memory, end_run_memory, 15.0))
        << "Memory grew during second period from " << mid_run_memory << " to " << end_run_memory << " bytes";
    
    // Overall test should not cause significant memory growth  
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_cleanup_memory, 15.0))
        << "Full pipeline test caused memory growth from " << pre_test_memory 
        << " to " << post_cleanup_memory << " bytes";
    
    // Note: We focus on pipeline memory stability rather than end-to-end client validation
    // The IntegrationHarness validates that the pipeline is actively processing frames
}

// Test 2: High-frequency full pipeline memory behavior
TEST_F(FullPipelineMemoryTest, HighFrequencyFullPipelineMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    IntegrationHarness harness;
    
    // High-frequency sensor configuration
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 128;
    sensor_config.height = 128;
    sensor_config.fps = 120.0; // High frequency
    sensor_config.pattern = SyntheticSensorDevice::Pattern::CHECKER;
    sensor_config.sensorId = "FullPipelineHighFreq";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_full_pipeline_highfreq";
    harness_config.max_width = 256;
    harness_config.max_height = 256;
    harness_config.processing_scale = 0.01f; // Higher processing load
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    // Collect memory samples during high-frequency operation
    std::vector<size_t> memory_samples;
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(500ms);
        memory_samples.push_back(MemoryUtils::getCurrentRSS());
    }
    
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should not show significant growth during high-frequency operation
    for (size_t i = 1; i < memory_samples.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(memory_samples[0], memory_samples[i], 18.0))
            << "Memory growth detected from sample 0 (" << memory_samples[0] 
            << ") to sample " << i << " (" << memory_samples[i] << ") bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 20.0))
        << "High-frequency full pipeline test caused overall memory growth from " << pre_test_memory 
        << " to " << final_memory << " bytes";
}

// Test 3: Multiple pipeline lifecycle memory management
TEST_F(FullPipelineMemoryTest, MultiplePipelineLifecycleMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test multiple full pipeline lifecycle cycles
    for (int cycle = 0; cycle < 3; ++cycle) {
        IntegrationHarness harness;
        
        SyntheticSensorDevice::Config sensor_config;
        sensor_config.width = 32;
        sensor_config.height = 32;
        sensor_config.fps = 60.0;
        sensor_config.pattern = SyntheticSensorDevice::Pattern::RADIAL;
        sensor_config.sensorId = "FullPipelineCycle" + std::to_string(cycle);
        
        harness.addSyntheticSensor(sensor_config);
        
        HarnessConfig harness_config;
        harness_config.shm_name = "/caldera_full_pipeline_cycle_" + std::to_string(cycle);
        harness_config.max_width = 64;
        harness_config.max_height = 64;
        harness_config.processing_scale = 0.001f;
        
        ASSERT_TRUE(harness.start(harness_config));
        
        // Run pipeline for this cycle
        std::this_thread::sleep_for(1s);
        
        // Stop and cleanup
        harness.stop();
        
        std::this_thread::sleep_for(100ms);
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Multiple cycles should not accumulate memory
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 12.0))
        << "Multiple full pipeline lifecycle cycles caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}

// Test 4: Processing scale impact on full pipeline memory
TEST_F(FullPipelineMemoryTest, ProcessingScaleFullPipelineMemoryImpact) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test different processing scales impact on full pipeline memory
    std::vector<float> scales = {0.001f, 0.01f, 0.1f, 0.5f};
    std::vector<size_t> memory_after_scale;
    
    for (float scale : scales) {
        IntegrationHarness harness;
        
        SyntheticSensorDevice::Config sensor_config;
        sensor_config.width = 64;
        sensor_config.height = 64;
        sensor_config.fps = 30.0;
        sensor_config.pattern = SyntheticSensorDevice::Pattern::STRIPES;
        sensor_config.sensorId = "FullPipelineScale" + std::to_string((int)(scale * 1000));
        
        harness.addSyntheticSensor(sensor_config);
        
        HarnessConfig harness_config;
        harness_config.shm_name = "/caldera_full_pipeline_scale_" + std::to_string((int)(scale * 1000));
        harness_config.max_width = 128;
        harness_config.max_height = 128;
        harness_config.processing_scale = scale;
        
        ASSERT_TRUE(harness.start(harness_config));
        
        // Run with current processing scale
        std::this_thread::sleep_for(1s);
        
        harness.stop();
        
        std::this_thread::sleep_for(100ms);
        
        memory_after_scale.push_back(MemoryUtils::getCurrentRSS());
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory usage should not grow dramatically with processing scale
    for (size_t i = 0; i < memory_after_scale.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, memory_after_scale[i], 25.0))
            << "Processing scale " << scales[i] << " caused excessive memory growth to " 
            << memory_after_scale[i] << " bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 20.0))
        << "Overall processing scale full pipeline test caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}

// Test 5: Multi-sensor full pipeline memory behavior
TEST_F(FullPipelineMemoryTest, MultiSensorFullPipelineMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    IntegrationHarness harness;
    
    // Add multiple synthetic sensors
    for (int i = 0; i < 3; ++i) {
        SyntheticSensorDevice::Config sensor_config;
        sensor_config.width = 48;
        sensor_config.height = 48;
        sensor_config.fps = 20.0; // Lower FPS to avoid overwhelming
        sensor_config.pattern = static_cast<SyntheticSensorDevice::Pattern>(i % 3); // Rotate through patterns
        sensor_config.sensorId = "MultiSensor" + std::to_string(i);
        
        harness.addSyntheticSensor(sensor_config);
    }
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_full_pipeline_multisensor";
    harness_config.max_width = 128;
    harness_config.max_height = 128;
    harness_config.processing_scale = 0.01f;
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    size_t active_memory = MemoryUtils::getCurrentRSS();
    
    // Run multi-sensor pipeline for extended period
    std::this_thread::sleep_for(3s);
    
    size_t end_memory = MemoryUtils::getCurrentRSS();
    
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should remain stable with multiple sensors
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(active_memory, end_memory, 20.0))
        << "Multi-sensor memory grew during operation from " << active_memory << " to " << end_memory << " bytes";
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 18.0))
        << "Multi-sensor full pipeline test caused memory growth from " << pre_test_memory 
        << " to " << final_memory << " bytes";
}