#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "common/Logger.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"
#include "processing/ProcessingManager.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;
using caldera::backend::processing::ProcessingManager;

class MemoryLeakTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!Logger::instance().isInitialized()) {
            Logger::instance().initialize("logs/test/memory_leak.log");
            Logger::instance().setGlobalLevel(spdlog::level::warn);  // Reduce noise for leak tests
        }
    }

    void TearDown() override {
        // Give time for any async cleanup
        std::this_thread::sleep_for(50ms);
    }
};

// Test for SharedMemory transport memory leaks
TEST_F(MemoryLeakTest, SharedMemoryTransport_CreateDestroy_NoLeaks) {
    const int iterations = 100;
    
    for (int i = 0; i < iterations; ++i) {
        auto logger = Logger::instance().get("Test.MemLeak.SHM");
        
        // Create and destroy transport server
        {
            SharedMemoryTransportServer::Config cfg;
            cfg.shm_name = "/caldera_memleak_test_" + std::to_string(i);
            cfg.max_width = 64;
            cfg.max_height = 64;
            
            auto server = std::make_unique<SharedMemoryTransportServer>(logger, cfg);
            server->start();
            
            // Send a few frames
            for (int frame = 0; frame < 5; ++frame) {
                caldera::backend::common::WorldFrame wf;
                wf.frame_id = frame;
                wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                wf.heightMap.width = 32;
                wf.heightMap.height = 32; 
                wf.heightMap.data.assign(32*32, static_cast<float>(frame + i));
                
                server->sendWorldFrame(wf);
            }
            
            server->stop();
        } // server should be fully destroyed here
        
        // Verify no leftover shared memory segments
        std::string shm_name = "/caldera_memleak_test_" + std::to_string(i);
        int fd = shm_open(shm_name.c_str(), O_RDONLY, 0644);
        if (fd >= 0) {
            close(fd);
            shm_unlink(shm_name.c_str());  // Cleanup if test failed to unlink
            FAIL() << "SharedMemory segment leaked: " << shm_name;
        }
    }
}

// Test for processing manager memory leaks
TEST_F(MemoryLeakTest, ProcessingManager_CreateDestroy_NoLeaks) {
    const int iterations = 50;
    
    for (int i = 0; i < iterations; ++i) {
        auto logger = Logger::instance().get("Test.MemLeak.Processing");
        
        {
            auto processing = std::make_unique<ProcessingManager>(logger, nullptr, 0.001f);
            
            // Set callback and process some frames
            int callback_count = 0;
            processing->setWorldFrameCallback([&callback_count](const caldera::backend::common::WorldFrame&) {
                callback_count++;
            });
            
            // Process several raw depth frames
            for (int frame = 0; frame < 10; ++frame) {
                caldera::backend::common::RawDepthFrame raw;
                raw.sensorId = "TEST_" + std::to_string(i);
                raw.width = 16;
                raw.height = 16;
                raw.data.assign(16*16, static_cast<uint16_t>(1000 + frame));
                raw.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                
                processing->processRawDepthFrame(raw);
            }
            
            EXPECT_EQ(callback_count, 10);
        } // processing should be fully destroyed here
        
        // Allow time for any cleanup
        std::this_thread::sleep_for(1ms);
    }
}

// Test for simple object memory leaks 
TEST_F(MemoryLeakTest, SimpleObjects_CreateDestroy_NoLeaks) {
    const int iterations = 50;
    
    for (int i = 0; i < iterations; ++i) {
        // Test vector allocations and deallocations (typical for height maps)
        {
            std::vector<float> height_data;
            height_data.resize(1024 * 1024);  // 1M floats = 4MB
            
            // Fill with data
            for (size_t j = 0; j < height_data.size(); ++j) {
                height_data[j] = static_cast<float>(j % 1000);
            }
            
            // Create some shared_ptr cycles and break them
            auto ptr1 = std::make_shared<std::vector<float>>(std::move(height_data));
            auto ptr2 = ptr1;
            
            EXPECT_EQ(ptr1.use_count(), 2);
            ptr2.reset();
            EXPECT_EQ(ptr1.use_count(), 1);
        } // All objects should be destroyed here
        
        // Test logger creation/destruction
        {
            auto test_logger = Logger::instance().get("Test.MemLeak.Simple." + std::to_string(i));
            test_logger->info("Test log message {}", i);
        }
        
        std::this_thread::sleep_for(1ms);
    }
}

// Stress test: create/destroy components in rapid succession
TEST_F(MemoryLeakTest, RapidCreateDestroy_StressTest) {
    const int iterations = 20;
    
    for (int i = 0; i < iterations; ++i) {
        auto logger = Logger::instance().get("Test.MemLeak.Stress");
        
        // Create multiple components simultaneously
        std::vector<std::thread> threads;
        
        for (int t = 0; t < 3; ++t) {
            threads.emplace_back([&logger, i, t]() {
                // SharedMemory component
                {
                    SharedMemoryTransportServer::Config cfg;
                    cfg.shm_name = "/caldera_stress_" + std::to_string(i) + "_" + std::to_string(t);
                    cfg.max_width = 32;
                    cfg.max_height = 32;
                    
                    auto server = std::make_unique<SharedMemoryTransportServer>(logger, cfg);
                    server->start();
                    
                    caldera::backend::common::WorldFrame wf;
                    wf.frame_id = i * 10 + t;
                    wf.heightMap.width = 16;
                    wf.heightMap.height = 16;
                    wf.heightMap.data.assign(16*16, 42.0f);
                    
                    server->sendWorldFrame(wf);
                    server->stop();
                }
                
                // Processing component  
                {
                    auto processing = std::make_unique<ProcessingManager>(logger, nullptr, 0.001f);
                    
                    caldera::backend::common::RawDepthFrame raw;
                    raw.sensorId = "STRESS";
                    raw.width = 8;
                    raw.height = 8;
                    raw.data.assign(64, 500);
                    
                    processing->processRawDepthFrame(raw);
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Cleanup any leftover shared memory
        for (int t = 0; t < 3; ++t) {
            std::string shm_name = "/caldera_stress_" + std::to_string(i) + "_" + std::to_string(t);
            shm_unlink(shm_name.c_str());  // Best effort cleanup
        }
    }
}

// Test for long-running memory usage stability
TEST_F(MemoryLeakTest, LongRunning_MemoryStability) {
    auto logger = Logger::instance().get("Test.MemLeak.LongRun");
    
    SharedMemoryTransportServer::Config cfg;
    cfg.shm_name = "/caldera_longrun";
    cfg.max_width = 128;
    cfg.max_height = 128;
    
    auto server = std::make_unique<SharedMemoryTransportServer>(logger, cfg);
    server->start();
    
    // Run for a while sending frames continuously
    auto start_time = std::chrono::steady_clock::now();
    const auto run_duration = 1s; // Keep test time reasonable
    uint64_t frame_id = 0;
    
    while (std::chrono::steady_clock::now() - start_time < run_duration) {
        caldera::backend::common::WorldFrame wf;
        wf.frame_id = frame_id++;
        wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        wf.heightMap.width = 64;
        wf.heightMap.height = 64;
        wf.heightMap.data.resize(64*64);
        
        // Fill with deterministic data
        for (size_t j = 0; j < wf.heightMap.data.size(); ++j) {
            wf.heightMap.data[j] = static_cast<float>(j % 1000);
        }
        
        server->sendWorldFrame(wf);
        
        // Small delay to prevent 100% CPU usage
        std::this_thread::sleep_for(1ms);
    }
    
    server->stop();
    
    // Should have sent many frames without issues
    auto stats = server->snapshotStats();
    EXPECT_GT(stats.frames_published, 100);
    EXPECT_EQ(stats.frames_dropped_capacity, 0);  // No drops expected at this rate
}