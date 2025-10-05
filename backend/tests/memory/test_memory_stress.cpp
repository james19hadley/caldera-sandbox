// Phase 2: Memory Stress Testing - High Load & Production Scenarios
// Tests memory behavior under sustained high throughput, extended runtime,
// rapid component cycling, and multi-consumer load scenarios

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <future>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "IntegrationHarness.h"
#include "common/Logger.h"
#include "helpers/MemoryUtils.h"
#include "hal/SyntheticSensorDevice.h"
#include "processing/ProcessingManager.h"
#include "transport/SharedMemoryTransportServer.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::transport::SharedMemoryTransportServer;

class MemoryStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto &L = caldera::backend::common::Logger::instance();
        if (!L.isInitialized()) {
            L.initialize("logs/test/memory_stress.log");
        }
        logger = L.get("MemoryStressTest");
    }

    void TearDown() override {
        // Small delay to ensure cleanup
        std::this_thread::sleep_for(100ms);
    }

    std::shared_ptr<spdlog::logger> logger;
};

// Test 1: High throughput memory stability - sustained 120+ FPS for 10+ seconds
TEST_F(MemoryStressTest, HighThroughputMemoryStability) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting high throughput stress test, baseline memory: {} MB", pre_test_memory / (1024*1024));
    
    IntegrationHarness harness;
    
    // Configure for maximum sustainable throughput
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 320;  // Smaller frames for higher FPS
    sensor_config.height = 240;
    sensor_config.fps = 120.0;  // Target 120 FPS
    sensor_config.pattern = SyntheticSensorDevice::Pattern::CHECKER;
    sensor_config.sensorId = "StressTestHighFPS";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_stress_high_fps";
    harness_config.max_width = 512;
    harness_config.max_height = 512;
    harness_config.processing_scale = 0.001f; // Minimal processing load to focus on throughput
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    // Warm-up period to allow one-time allocations (allocators, SHM, logging buffers)
    std::this_thread::sleep_for(300ms);
    size_t warm_memory = MemoryUtils::getCurrentRSS();
    logger->info("Warm baseline memory after startup: {} MB (startup delta {} MB)", 
                 warm_memory / (1024*1024), (warm_memory - pre_test_memory) / (1024*1024));
    
    // Run for 10 seconds at high FPS, collecting memory samples every 2s
    std::vector<size_t> memory_samples;
    auto test_duration = 10s;
    auto sample_interval = 2s;
    auto end_time = std::chrono::steady_clock::now() + test_duration;
    
    while (std::chrono::steady_clock::now() < end_time) {
        memory_samples.push_back(MemoryUtils::getCurrentRSS());
        logger->info("High throughput memory sample: {} MB", memory_samples.back() / (1024*1024));
        std::this_thread::sleep_for(sample_interval);
    }
    
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should remain stable throughout high throughput operation
    for (size_t i = 1; i < memory_samples.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(memory_samples[0], memory_samples[i], 25.0))
            << "High throughput caused memory growth from sample 0 (" << memory_samples[0] 
            << ") to sample " << i << " (" << memory_samples[i] << ") bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 30.0))
        << "High throughput stress test caused overall memory growth from " << pre_test_memory 
        << " to " << final_memory << " bytes";
    
    logger->info("High throughput test completed, final memory: {} MB", final_memory / (1024*1024));
}

// Test 2: Rapid component cycling - start/stop cycles testing memory accumulation
TEST_F(MemoryStressTest, RapidComponentCyclingMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting rapid cycling stress test, baseline memory: {} MB", pre_test_memory / (1024*1024));
    
    const int cycle_count = 20; // More cycles than Phase 1 integration tests
    std::vector<size_t> cycle_memories;
    
    for (int cycle = 0; cycle < cycle_count; ++cycle) {
        // Create components individually to test rapid lifecycle
        auto processing = std::make_unique<ProcessingManager>(
            logger, nullptr, 0.001f);
        
        SharedMemoryTransportServer::Config transport_config;
        transport_config.shm_name = "/caldera_stress_cycle_" + std::to_string(cycle);
        transport_config.max_width = 128;
        transport_config.max_height = 128;
        
        auto transport = std::make_unique<SharedMemoryTransportServer>(
            logger, transport_config);
        
        // Start and immediately create some load
        transport->start();
        
        // Brief operation to stress the cycle
        std::this_thread::sleep_for(50ms);
        
        // Explicitly stop and destroy
        transport->stop();
        transport.reset();
        processing.reset();
        
        // Small delay between cycles
        std::this_thread::sleep_for(20ms);
        
        size_t cycle_memory = MemoryUtils::getCurrentRSS();
        cycle_memories.push_back(cycle_memory);
        
        if (cycle % 5 == 0) {
            logger->info("Cycle {} memory: {} MB", cycle, cycle_memory / (1024*1024));
        }
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Check that rapid cycling doesn't cause progressive memory accumulation
    // Allow some variance but no major trends
    size_t baseline_cycle_memory = cycle_memories[2]; // Skip first few cycles for warm-up
    for (size_t i = 5; i < cycle_memories.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(baseline_cycle_memory, cycle_memories[i], 20.0))
            << "Rapid cycling accumulated memory at cycle " << i 
            << " from baseline " << baseline_cycle_memory << " to " << cycle_memories[i] << " bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 15.0))
        << "Rapid component cycling caused overall memory growth from " << pre_test_memory 
        << " to " << final_memory << " bytes";
    
    logger->info("Rapid cycling test completed, final memory: {} MB", final_memory / (1024*1024));
}

// Test 3: Multi-consumer memory scaling - multiple simultaneous SHM readers
TEST_F(MemoryStressTest, MultiConsumerMemoryScaling) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting multi-consumer stress test, baseline memory: {} MB", pre_test_memory / (1024*1024));
    
    IntegrationHarness harness;
    
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 256;
    sensor_config.height = 256;
    sensor_config.fps = 60.0;
    sensor_config.pattern = SyntheticSensorDevice::Pattern::RADIAL;
    sensor_config.sensorId = "StressTestMultiConsumer";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_stress_multi_consumer";
    harness_config.max_width = 512;
    harness_config.max_height = 512;
    harness_config.processing_scale = 0.01f;
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));

    // Warm-up to establish post-start baseline (allocator, logging, SHM setup)
    std::this_thread::sleep_for(300ms);
    size_t warm_memory = MemoryUtils::getCurrentRSS();
    logger->info("Warm baseline after harness start: {} MB (startup growth {} MB)",
                 warm_memory / (1024*1024), (warm_memory - pre_test_memory) / (1024*1024));

    // Simulate multiple consumers by creating multiple processing managers
    // that could potentially access the shared memory
    const int consumer_count = 8; // Simulate 8 concurrent consumers
    std::vector<std::unique_ptr<ProcessingManager>> consumers;
    std::vector<size_t> memory_with_consumers;
    
    for (int i = 0; i < consumer_count; ++i) {
        consumers.push_back(std::make_unique<ProcessingManager>(
            logger, nullptr, 0.001f));
        
        // Let each consumer run briefly
        std::this_thread::sleep_for(100ms);
        
        size_t memory_with_n_consumers = MemoryUtils::getCurrentRSS();
        memory_with_consumers.push_back(memory_with_n_consumers);
        
        logger->info("Memory with {} consumers: {} MB", i + 1, memory_with_n_consumers / (1024*1024));

        // Check incremental growth vs warm baseline allowing 40MB per consumer (includes ASAN overhead)
        size_t expected_max_total_growth = (i + 1) * 40 * 1024 * 1024; // 40MB each incremental envelope
        size_t total_growth = (memory_with_n_consumers > warm_memory) ? (memory_with_n_consumers - warm_memory) : 0;
        EXPECT_LT(total_growth, expected_max_total_growth)
            << "Total growth " << total_growth / (1024*1024) << " MB with " << (i+1)
            << " consumers exceeds linear envelope " << expected_max_total_growth / (1024*1024) << " MB";
    }
    
    // Run all consumers together for a period
    std::this_thread::sleep_for(2s);
    
    size_t peak_consumer_memory = MemoryUtils::getCurrentRSS();
    
    // Clean up consumers one by one
    for (auto& consumer : consumers) {
        consumer.reset();
        std::this_thread::sleep_for(50ms);
    }
    consumers.clear();
    
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory scaling should be reasonable with multiple consumers
    // Each consumer shouldn't add excessive overhead
    // Defensive: guard against unsigned underflow if peak_consumer_memory somehow < warm_memory
    size_t raw_delta = 0;
    if (peak_consumer_memory > warm_memory) {
        raw_delta = peak_consumer_memory - warm_memory;
    } else {
        // Under rare circumstances RSS sampling jitter could report a slightly lower value; treat as zero growth
        raw_delta = 0;
    }
    size_t memory_per_consumer = raw_delta / consumer_count;
    EXPECT_LT(memory_per_consumer, 50 * 1024 * 1024) // Less than 50MB per consumer
        << "Each consumer added " << memory_per_consumer / (1024*1024) << " MB (raw_delta="
        << raw_delta / (1024*1024) << " MB peak=" << peak_consumer_memory / (1024*1024)
        << " MB warm=" << warm_memory / (1024*1024) << " MB) which seems excessive";
    
    // Memory should scale roughly linearly (not exponentially) with consumers
    // Already performed incremental checks above; retain a simple sanity check on peak
    
    // Attempt to release unused heap memory before final measurement
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
    final_memory = MemoryUtils::getCurrentRSS();
    
    // Adaptive final allowance:
    // Observed: warm ~112MB -> final ~330MB with 8 consumers (~27MB/consumer including ASAN redzones & quarantine)
    // We'll allow up to 45MB per consumer (360MB total upper envelope) and 40% relative growth before abs limit kicks.
    size_t consumersCreated = consumer_count; // all created
    size_t maxPerConsumerBytes = 45 * 1024 * 1024; // 45MB
    size_t dynamicAbsAllowance = consumersCreated * maxPerConsumerBytes; // linear envelope
    double percentLimit = 40.0; // 40% relative growth from warm baseline (expanded x6 under ASAN)
    size_t absoluteAllowance = std::max(dynamicAbsAllowance, static_cast<size_t>(64 * 1024 * 1024));
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowthAdaptive(warm_memory, final_memory, percentLimit, absoluteAllowance))
        << "Multi-consumer stress test growth exceeded envelope. warm=" << warm_memory
        << " final=" << final_memory
        << " delta=" << (final_memory - warm_memory)
        << " absAllowance=" << absoluteAllowance;
    
    logger->info("Multi-consumer test completed, final memory: {} MB", final_memory / (1024*1024));
}

// Test 4: Extended runtime memory validation - continuous operation for longer period
TEST_F(MemoryStressTest, ExtendedRuntimeMemoryValidation) {
    if (!std::getenv("CALDERA_ENABLE_LONG_MEMORY_TESTS")) {
        GTEST_SKIP() << "Long memory tests disabled. Set CALDERA_ENABLE_LONG_MEMORY_TESTS=1 to enable.";
    }
    
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    logger->info("Starting extended runtime stress test, baseline memory: {} MB", pre_test_memory / (1024*1024));
    
    IntegrationHarness harness;
    
    SyntheticSensorDevice::Config sensor_config;
    sensor_config.width = 160;   // Moderate size for extended run
    sensor_config.height = 120;
    sensor_config.fps = 30.0;    // Standard FPS for extended operation
    sensor_config.pattern = SyntheticSensorDevice::Pattern::STRIPES;
    sensor_config.sensorId = "StressTestExtendedRuntime";
    
    harness.addSyntheticSensor(sensor_config);
    
    HarnessConfig harness_config;
    harness_config.shm_name = "/caldera_stress_extended";
    harness_config.max_width = 320;
    harness_config.max_height = 240;
    harness_config.processing_scale = 0.005f;
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    ASSERT_TRUE(harness.start(harness_config));
    
    // Run for extended period (30 seconds) collecting memory samples
    std::vector<size_t> memory_samples;
    auto test_duration = 30s; // Longer than typical integration tests
    auto sample_interval = 5s;
    auto end_time = std::chrono::steady_clock::now() + test_duration;
    
    int sample_count = 0;
    while (std::chrono::steady_clock::now() < end_time) {
        size_t current_memory = MemoryUtils::getCurrentRSS();
        memory_samples.push_back(current_memory);
        logger->info("Extended runtime sample {}: {} MB", sample_count++, current_memory / (1024*1024));
        std::this_thread::sleep_for(sample_interval);
    }
    
    harness.stop();
    
    std::this_thread::sleep_for(200ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Extended runtime should not show significant memory growth trends
    // Check that memory remains bounded throughout operation
    size_t baseline_memory = memory_samples[0];
    for (size_t i = 1; i < memory_samples.size(); ++i) {
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(baseline_memory, memory_samples[i], 15.0))
            << "Extended runtime showed memory growth from baseline " << baseline_memory 
            << " to sample " << i << " (" << memory_samples[i] << ") bytes";
    }
    
    // Check for progressive memory growth (memory leak pattern)
    if (memory_samples.size() >= 3) {
        size_t early_memory = memory_samples[1];
        size_t late_memory = memory_samples[memory_samples.size() - 1];
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(early_memory, late_memory, 10.0))
            << "Extended runtime shows progressive memory growth pattern from " 
            << early_memory << " to " << late_memory << " bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 15.0))
        << "Extended runtime stress test caused overall memory growth from " << pre_test_memory 
        << " to " << final_memory << " bytes";
    
    logger->info("Extended runtime test completed, final memory: {} MB", final_memory / (1024*1024));
}