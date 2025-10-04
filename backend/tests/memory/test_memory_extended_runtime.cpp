// Phase 2: Extended Runtime Memory Testing
// Tests for long-running memory validation, gradual growth detection,
// and memory fragmentation resistance over extended periods

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>

#include "IntegrationHarness.h"
#include "common/Logger.h"
#include "helpers/MemoryUtils.h"
#include "hal/SyntheticSensorDevice.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;

class ExtendedRuntimeMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto &L = caldera::backend::common::Logger::instance();
        if (!L.isInitialized()) {
            L.initialize("logs/test/extended_runtime_memory.log");
        }
        logger = L.get("ExtendedRuntimeMemoryTest");
    }

    void TearDown() override {
        std::this_thread::sleep_for(200ms);
    }

    std::shared_ptr<spdlog::logger> logger;
    
    // Helper to detect linear memory growth trends
    bool detectMemoryGrowthTrend(const std::vector<size_t>& samples, double max_growth_mb_per_min) {
        if (samples.size() < 3) return false;
        
        // Simple linear regression to detect growth trend
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        size_t n = samples.size();
        
        for (size_t i = 0; i < n; ++i) {
            double x = static_cast<double>(i);
            double y = static_cast<double>(samples[i]) / (1024.0 * 1024.0); // Convert to MB
            
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
        }
        
        // Calculate slope (MB per sample)
        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
        
        // Convert slope to MB per minute (assuming samples taken every minute)
        double growth_rate_mb_per_min = slope;
        
        logger->info("Memory growth trend analysis: {} MB/min (max allowed: {} MB/min)", 
                     growth_rate_mb_per_min, max_growth_mb_per_min);
        
        return growth_rate_mb_per_min <= max_growth_mb_per_min;
    }
};

// Test 1: Long-term continuous operation (5+ minutes)
TEST_F(ExtendedRuntimeMemoryTest, LongTermContinuousOperation) {
    if (!std::getenv("CALDERA_ENABLE_LONG_MEMORY_TESTS")) {
        GTEST_SKIP() << "Long memory tests disabled. Set CALDERA_ENABLE_LONG_MEMORY_TESTS=1 to enable.";
    }
    
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting long-term continuous operation test, baseline memory: {} MB", 
                 pre_test_memory / (1024*1024));
    
    IntegrationHarness harness;
    
    // Configure for stable long-term operation
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 320;
    sensor_config.height = 240;
    sensor_config.fps = 30.0; // Standard FPS for stability
    sensor_config.pattern = SyntheticSensorDevice::Pattern::CHECKER;
    sensor_config.sensorId = "LongTermTest";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_longterm_test";
    harness_config.max_width = 640;
    harness_config.max_height = 480;
    harness_config.processing_scale = 0.01f;
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    // Run for 5 minutes, sampling every 30 seconds
    std::vector<size_t> memory_samples;
    auto test_duration = 5min; // Significantly longer than stress tests
    auto sample_interval = 30s; // Frequent sampling to catch gradual leaks
    auto end_time = std::chrono::steady_clock::now() + test_duration;
    
    int sample_count = 0;
    while (std::chrono::steady_clock::now() < end_time) {
        size_t current_memory = MemoryUtils::getCurrentRSS();
        memory_samples.push_back(current_memory);
        logger->info("Long-term sample {} ({}s): {} MB", 
                     sample_count, sample_count * 30, current_memory / (1024*1024));
        sample_count++;
        std::this_thread::sleep_for(sample_interval);
    }
    
    harness.stop();
    
    std::this_thread::sleep_for(300ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Check for gradual memory growth trends (should be < 0.1 MB/min)
    EXPECT_TRUE(detectMemoryGrowthTrend(memory_samples, 0.1))
        << "Long-term operation shows concerning memory growth trend";
    
    // Overall memory growth should be minimal for long-term stability
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 10.0))
        << "Long-term continuous operation caused excessive memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
    
    // Check memory stability in final phase (last 25% of samples)
    if (memory_samples.size() >= 4) {
        size_t quarter_point = memory_samples.size() * 3 / 4;
        size_t late_baseline = memory_samples[quarter_point];
        size_t final_sample = memory_samples.back();
        
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(late_baseline, final_sample, 5.0))
            << "Memory continued growing in final phase of long-term test";
    }
    
    logger->info("Long-term continuous operation completed, final memory: {} MB", 
                 final_memory / (1024*1024));
}

// Test 2: Memory fragmentation resistance - many allocate/deallocate cycles
TEST_F(ExtendedRuntimeMemoryTest, MemoryFragmentationResistance) {
    if (!std::getenv("CALDERA_ENABLE_LONG_MEMORY_TESTS")) {
        GTEST_SKIP() << "Long memory tests disabled. Set CALDERA_ENABLE_LONG_MEMORY_TESTS=1 to enable.";
    }
    
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting memory fragmentation resistance test, baseline memory: {} MB", 
                 pre_test_memory / (1024*1024));
    
    // Test with many rapid start/stop cycles to test fragmentation resistance
    const int fragmentation_cycles = 50;
    std::vector<size_t> cycle_memories;
    
    for (int cycle = 0; cycle < fragmentation_cycles; ++cycle) {
        IntegrationHarness harness;
        
        SyntheticSensorDevice::Config sensor_config;
        sensor_config.width = 128 + (cycle % 4) * 32; // Vary sizes to test fragmentation
        sensor_config.height = 96 + (cycle % 4) * 24;
        sensor_config.fps = 60.0;
        sensor_config.pattern = static_cast<SyntheticSensorDevice::Pattern>(cycle % 3);
        sensor_config.sensorId = "FragTest" + std::to_string(cycle);
        
        harness.addSyntheticSensor(sensor_config);
        
        HarnessConfig harness_config;
        harness_config.shm_name = "/caldera_frag_test_" + std::to_string(cycle);
        harness_config.max_width = 256;
        harness_config.max_height = 256;
        harness_config.processing_scale = 0.001f; // Minimal processing
        
        ASSERT_TRUE(harness.start(harness_config));
        
        // Brief operation
        std::this_thread::sleep_for(100ms);
        
        harness.stop();
        
        // Small delay between cycles
        std::this_thread::sleep_for(50ms);
        
        size_t cycle_memory = MemoryUtils::getCurrentRSS();
        cycle_memories.push_back(cycle_memory);
        
        if (cycle % 10 == 0) {
            logger->info("Fragmentation cycle {} memory: {} MB", cycle, cycle_memory / (1024*1024));
        }
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should not show progressive fragmentation-induced growth
    // Allow some variance but check that later cycles don't consistently use more memory
    if (cycle_memories.size() >= 20) {
        std::vector<size_t> early_memories(cycle_memories.begin() + 5, cycle_memories.begin() + 15);
        std::vector<size_t> late_memories(cycle_memories.end() - 10, cycle_memories.end());
        
        double early_avg = std::accumulate(early_memories.begin(), early_memories.end(), 0.0) / early_memories.size();
        double late_avg = std::accumulate(late_memories.begin(), late_memories.end(), 0.0) / late_memories.size();
        
        double growth_percent = ((late_avg - early_avg) / early_avg) * 100.0;
        
        EXPECT_LT(growth_percent, 15.0)
            << "Memory fragmentation test shows " << growth_percent 
            << "% growth from early to late cycles, suggesting fragmentation";
        
        logger->info("Fragmentation analysis: early_avg={} MB, late_avg={} MB, growth={}%",
                     early_avg / (1024*1024), late_avg / (1024*1024), growth_percent);
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 20.0))
        << "Memory fragmentation resistance test caused overall memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
    
    logger->info("Memory fragmentation resistance test completed, final memory: {} MB", 
                 final_memory / (1024*1024));
}

// Test 3: Periodic cleanup validation - detect if periodic cleanup is working
TEST_F(ExtendedRuntimeMemoryTest, PeriodicCleanupValidation) {
    if (!std::getenv("CALDERA_ENABLE_LONG_MEMORY_TESTS")) {
        GTEST_SKIP() << "Long memory tests disabled. Set CALDERA_ENABLE_LONG_MEMORY_TESTS=1 to enable.";
    }
    
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting periodic cleanup validation test, baseline memory: {} MB", 
                 pre_test_memory / (1024*1024));
    
    IntegrationHarness harness;
    
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 256;
    sensor_config.height = 192;
    sensor_config.fps = 45.0;
    sensor_config.pattern = SyntheticSensorDevice::Pattern::RADIAL;
    sensor_config.sensorId = "PeriodicCleanupTest";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_cleanup_test";
    harness_config.max_width = 512;
    harness_config.max_height = 384;
    harness_config.processing_scale = 0.02f;
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    // Run test with periods of activity and rest to see cleanup behavior
    std::vector<std::pair<std::string, size_t>> memory_checkpoints;
    
    // Phase 1: Initial operation
    std::this_thread::sleep_for(30s);
    memory_checkpoints.emplace_back("after_initial_30s", MemoryUtils::getCurrentRSS());
    
    // Phase 2: Extended operation
    std::this_thread::sleep_for(60s);
    memory_checkpoints.emplace_back("after_60s_more", MemoryUtils::getCurrentRSS());
    
    // Phase 3: Final operation
    std::this_thread::sleep_for(30s);
    memory_checkpoints.emplace_back("after_final_30s", MemoryUtils::getCurrentRSS());
    
    harness.stop();
    
    // Phase 4: After cleanup
    std::this_thread::sleep_for(500ms);
    memory_checkpoints.emplace_back("after_cleanup", MemoryUtils::getCurrentRSS());
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Log all checkpoints
    for (const auto& checkpoint : memory_checkpoints) {
        logger->info("Cleanup checkpoint '{}': {} MB", 
                     checkpoint.first, checkpoint.second / (1024*1024));
    }
    
    // Memory should not grow significantly between phases
    for (size_t i = 1; i < memory_checkpoints.size() - 1; ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(
            memory_checkpoints[0].second, 
            memory_checkpoints[i].second, 15.0))
            << "Memory grew significantly at checkpoint '" << memory_checkpoints[i].first << "'";
    }
    
    // Final cleanup should show memory reduction
    size_t pre_cleanup_memory = memory_checkpoints[memory_checkpoints.size() - 2].second;
    size_t post_cleanup_memory = memory_checkpoints.back().second;
    
    EXPECT_LE(post_cleanup_memory, pre_cleanup_memory * 1.05) // Allow 5% variance
        << "Cleanup did not reduce memory usage as expected";
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 12.0))
        << "Periodic cleanup validation test caused overall memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
    
    logger->info("Periodic cleanup validation completed, final memory: {} MB", 
                 final_memory / (1024*1024));
}

// Test 4: Memory behavior under sustained processing load
TEST_F(ExtendedRuntimeMemoryTest, SustainedProcessingLoadMemory) {
    if (!std::getenv("CALDERA_ENABLE_LONG_MEMORY_TESTS")) {
        GTEST_SKIP() << "Long memory tests disabled. Set CALDERA_ENABLE_LONG_MEMORY_TESTS=1 to enable.";
    }
    
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting sustained processing load test, baseline memory: {} MB", 
                 pre_test_memory / (1024*1024));
    
    IntegrationHarness harness;
    
    // Configure for sustained processing load
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 480;
    sensor_config.height = 360;
    sensor_config.fps = 60.0;
    sensor_config.pattern = SyntheticSensorDevice::Pattern::STRIPES;
    sensor_config.sensorId = "SustainedProcessingTest";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_sustained_processing";
    harness_config.max_width = 640;
    harness_config.max_height = 480;
    harness_config.processing_scale = 0.1f; // Higher processing load than stress tests
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    // Run with sustained processing for 3 minutes
    std::vector<size_t> memory_samples;
    auto test_duration = 3min;
    auto sample_interval = 20s;
    auto end_time = std::chrono::steady_clock::now() + test_duration;
    
    int sample_count = 0;
    while (std::chrono::steady_clock::now() < end_time) {
        size_t current_memory = MemoryUtils::getCurrentRSS();
        memory_samples.push_back(current_memory);
        logger->info("Sustained processing sample {}: {} MB", 
                     sample_count++, current_memory / (1024*1024));
        std::this_thread::sleep_for(sample_interval);
    }
    
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should remain stable under sustained processing load
    EXPECT_TRUE(detectMemoryGrowthTrend(memory_samples, 0.2))
        << "Sustained processing load shows concerning memory growth trend";
    
    // Check that processing load doesn't cause excessive memory usage
    size_t max_memory = *std::max_element(memory_samples.begin(), memory_samples.end());
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(start_memory, max_memory, 25.0))
        << "Sustained processing load caused excessive peak memory usage";
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 15.0))
        << "Sustained processing load test caused overall memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
    
    logger->info("Sustained processing load test completed, final memory: {} MB", 
                 final_memory / (1024*1024));
}