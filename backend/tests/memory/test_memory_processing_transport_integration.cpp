// Phase 1: Processing→Transport Memory Integration Tests
// Tests memory behavior when ProcessingManager outputs are consumed by SharedMemoryTransportServer
// Focus: Memory stability during WorldFrame transmission and shared memory operations

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>

#include "processing/ProcessingManager.h"
#include "transport/SharedMemoryTransportServer.h"
#include "common/Logger.h"
#include "helpers/MemoryUtils.h"
#include "helpers/TestCalderaClient.h"

using namespace std::chrono_literals;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;

class ProcessingTransportMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!caldera::backend::common::Logger::instance().isInitialized()) {
            caldera::backend::common::Logger::instance().initialize("logs/test/processing_transport_memory.log");
        }
        baseline_memory_ = MemoryUtils::getCurrentRSS();
    }

    void TearDown() override {
        size_t final_memory = MemoryUtils::getCurrentRSS();
        double limit = 8.0 * (MemoryUtils::isAsan() ? 6.0 : 1.0);
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(baseline_memory_, final_memory, limit))
            << "Memory grew from " << baseline_memory_ << " to " << final_memory << " bytes ("
            << MemoryUtils::calculateGrowthPercent(baseline_memory_, final_memory) << "% growth)";
    }

    std::shared_ptr<spdlog::logger> logger(const std::string& name) {
        return caldera::backend::common::Logger::instance().get(name);
    }

    // Helper method to create a synthetic depth frame
    RawDepthFrame createTestDepthFrame(const std::string& sensor_id, uint16_t width, uint16_t height, float scale = 1.0f) {
        RawDepthFrame frame;
        frame.sensorId = sensor_id;
        frame.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        frame.width = width;
        frame.height = height;
        frame.data.resize(width * height);
        
        // Fill with test pattern - gradient based on position and scale
        for (uint16_t y = 0; y < height; ++y) {
            for (uint16_t x = 0; x < width; ++x) {
                float normalized = ((float)(x + y)) / (width + height);
                frame.data[y * width + x] = static_cast<uint16_t>(1000 + normalized * 2000 * scale);
            }
        }
        return frame;
    }

private:
    size_t baseline_memory_;
};

// Test 1: Basic Processing→Transport pipeline memory stability
TEST_F(ProcessingTransportMemoryTest, BasicPipelineMemoryStability) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Create transport server
    SharedMemoryTransportServer::Config transport_config;
    transport_config.shm_name = "/caldera_proc_trans_test_1";
    transport_config.max_width = 128;
    transport_config.max_height = 128;
    
    auto transport = std::make_unique<SharedMemoryTransportServer>(
        logger("ProcessingTransportTest.Transport"), transport_config);
    
    transport->start();
    
    // Create processing manager
    std::atomic<uint32_t> world_frames_sent{0};
    auto processing_manager = std::make_unique<ProcessingManager>(
        logger("ProcessingTransportTest.Processing"), 
        nullptr, // fusion logger
        0.001f // minimal processing scale
    );
    
    // Connect processing output to transport
    processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
        transport->sendWorldFrame(frame);
        world_frames_sent.fetch_add(1);
    });
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    // Process frames through the pipeline
    for (uint32_t i = 0; i < 60; ++i) {
        RawDepthFrame depth_frame = createTestDepthFrame("ProcTransTest1", 64, 64);
        processing_manager->processRawDepthFrame(depth_frame);
        std::this_thread::sleep_for(10ms); // Simulate realistic timing
    }
    
    size_t mid_memory = MemoryUtils::getCurrentRSS();
    
    // Process more frames to check for memory growth
    for (uint32_t i = 60; i < 120; ++i) {
        RawDepthFrame depth_frame = createTestDepthFrame("ProcTransTest1", 64, 64);
        processing_manager->processRawDepthFrame(depth_frame);
        std::this_thread::sleep_for(10ms);
    }
    
    size_t end_memory = MemoryUtils::getCurrentRSS();
    
    // Cleanup
    transport->stop();
    processing_manager.reset();
    transport.reset();
    
    std::this_thread::sleep_for(100ms);
    size_t post_cleanup_memory = MemoryUtils::getCurrentRSS();
    
    // Validate frame processing occurred
    EXPECT_GT(world_frames_sent.load(), 110u) << "Expected significant WorldFrame transmission";
    
    // Memory should remain stable during operation
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(start_memory, mid_memory, 15.0))
        << "Memory grew during first batch from " << start_memory << " to " << mid_memory << " bytes";
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(mid_memory, end_memory, 10.0))
        << "Memory grew during second batch from " << mid_memory << " to " << end_memory << " bytes";
    
    // Overall test should not cause significant memory growth
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_cleanup_memory, 12.0))
        << "Processing→Transport test caused memory growth from " << pre_test_memory 
        << " to " << post_cleanup_memory << " bytes";
}

// Test 2: High-throughput transport memory behavior
TEST_F(ProcessingTransportMemoryTest, HighThroughputTransportMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Larger shared memory for high throughput
    SharedMemoryTransportServer::Config transport_config;
    transport_config.shm_name = "/caldera_proc_trans_highthru";
    transport_config.max_width = 256;
    transport_config.max_height = 256;
    
    auto transport = std::make_unique<SharedMemoryTransportServer>(
        logger("ProcessingTransportTest.HighThru.Transport"), transport_config);
    
    transport->start();
    
    std::atomic<uint32_t> world_frames_sent{0};
    auto processing_manager = std::make_unique<ProcessingManager>(
        logger("ProcessingTransportTest.HighThru.Processing"),
        nullptr, // fusion logger  
        0.01f // higher processing scale
    );
    
    processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
        transport->sendWorldFrame(frame);
        world_frames_sent.fetch_add(1);
    });
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    // High-frequency processing with larger frames
    std::vector<size_t> memory_samples;
    for (int batch = 0; batch < 5; ++batch) {
        for (uint32_t i = 0; i < 30; ++i) {
            RawDepthFrame depth_frame = createTestDepthFrame("ProcTransHighThru", 128, 128, 1.5f);
            processing_manager->processRawDepthFrame(depth_frame);
            std::this_thread::sleep_for(5ms); // High frequency
        }
        memory_samples.push_back(MemoryUtils::getCurrentRSS());
    }
    
    size_t end_memory = MemoryUtils::getCurrentRSS();
    
    transport->stop();
    processing_manager.reset();
    transport.reset();
    
    std::this_thread::sleep_for(100ms);
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Validate high throughput achieved
    EXPECT_GT(world_frames_sent.load(), 140u) << "Expected high WorldFrame throughput";
    
    // Allow first sample as warm-up; check incremental growth between subsequent samples
    for (size_t i = 2; i < memory_samples.size(); ++i) {
        size_t prev = memory_samples[i-1];
        size_t cur = memory_samples[i];
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(prev, cur, 18.0))
            << "Incremental memory growth batch " << (i-1) << " -> " << i << " exceeded threshold (" << prev << " -> " << cur << ")";
    }

    size_t stabilized_baseline = memory_samples.back();
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowthAdaptive(stabilized_baseline, final_memory, 20.0, 50 * 1024 * 1024))
        << "Post-cleanup memory did not stabilize (" << stabilized_baseline << " -> " << final_memory << ") delta="
        << (final_memory > stabilized_baseline ? final_memory - stabilized_baseline : 0) << " bytes";
}

// Test 3: Transport lifecycle memory management
TEST_F(ProcessingTransportMemoryTest, TransportLifecycleMemory) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test multiple transport lifecycle cycles
    for (int cycle = 0; cycle < 3; ++cycle) {
        std::atomic<uint32_t> frames_in_cycle{0};
        
        SharedMemoryTransportServer::Config transport_config;
        transport_config.shm_name = "/caldera_proc_trans_cycle_" + std::to_string(cycle);
        transport_config.max_width = 64;
        transport_config.max_height = 64;
        
        auto transport = std::make_unique<SharedMemoryTransportServer>(
            logger("ProcessingTransportTest.Lifecycle.Transport." + std::to_string(cycle)), transport_config);
        
        transport->start();
        
        auto processing_manager = std::make_unique<ProcessingManager>(
            logger("ProcessingTransportTest.Lifecycle.Processing." + std::to_string(cycle)),
            nullptr, // fusion logger
            0.001f
        );
        
        processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
            transport->sendWorldFrame(frame);
            frames_in_cycle.fetch_add(1);
        });
        
        // Process frames for this cycle
        for (uint32_t i = 0; i < 20; ++i) {
            RawDepthFrame depth_frame = createTestDepthFrame("ProcTransCycle" + std::to_string(cycle), 32, 32);
            processing_manager->processRawDepthFrame(depth_frame);
            std::this_thread::sleep_for(15ms);
        }
        
        // Stop and cleanup
        transport->stop();
        transport.reset();
        processing_manager.reset();
        
        std::this_thread::sleep_for(50ms);
        
        EXPECT_GT(frames_in_cycle.load(), 15u) << "Cycle " << cycle << " should process frames";
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Multiple cycles should not accumulate memory
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 10.0))
        << "Multiple transport lifecycle cycles caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}

// Test 4: Shared memory size scaling memory impact
TEST_F(ProcessingTransportMemoryTest, SharedMemoryScalingMemoryImpact) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test different shared memory sizes and their memory impact
    std::vector<std::pair<uint16_t, uint16_t>> sizes = {{32, 32}, {64, 64}, {128, 128}, {256, 256}};
    std::vector<size_t> memory_after_size;
    
    for (size_t idx = 0; idx < sizes.size(); ++idx) {
        auto [width, height] = sizes[idx];
        
        std::atomic<uint32_t> frames_processed{0};
        
        SharedMemoryTransportServer::Config transport_config;
        transport_config.shm_name = "/caldera_proc_trans_size_" + std::to_string(width);
        transport_config.max_width = width;
        transport_config.max_height = height;
        
        auto transport = std::make_unique<SharedMemoryTransportServer>(
            logger("ProcessingTransportTest.Size.Transport"), transport_config);
        
        transport->start();
        
        auto processing_manager = std::make_unique<ProcessingManager>(
            logger("ProcessingTransportTest.Size.Processing"),
            nullptr, // fusion logger
            0.01f
        );
        
        processing_manager->setWorldFrameCallback([&](const WorldFrame& frame) {
            transport->sendWorldFrame(frame);
            frames_processed.fetch_add(1);
        });
        
        // Process frames with current size
        for (uint32_t i = 0; i < 15; ++i) {
            RawDepthFrame depth_frame = createTestDepthFrame("ProcTransSize", width/2, height/2);
            processing_manager->processRawDepthFrame(depth_frame);
            std::this_thread::sleep_for(20ms);
        }
        
        transport->stop();
        transport.reset();
        processing_manager.reset();
        
        std::this_thread::sleep_for(100ms);
        
        memory_after_size.push_back(MemoryUtils::getCurrentRSS());
        
        EXPECT_GT(frames_processed.load(), 10u) 
            << "Size " << width << "x" << height << " should process frames";
    }
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    
    // Memory usage should not grow dramatically with shared memory size
    for (size_t i = 0; i < memory_after_size.size(); ++i) {
        auto [width, height] = sizes[i];
        EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, memory_after_size[i], 20.0))
            << "Shared memory size " << width << "x" << height 
            << " caused excessive memory growth to " << memory_after_size[i] << " bytes";
    }
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 18.0))
        << "Overall shared memory scaling test caused memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}