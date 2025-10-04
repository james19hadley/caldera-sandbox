#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <fstream>

#include "processing/ProcessingManager.h"
#include "common/DataTypes.h"
#include "helpers/MemoryUtils.h"

using namespace caldera::backend::processing;
using namespace caldera::backend::common;

class ProcessingMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        baseline_memory_ = MemoryUtils::getCurrentRSS();
    }

    void TearDown() override {
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

TEST_F(ProcessingMemoryTest, ProcessingManagerLifecycle) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test multiple create/destroy cycles of ProcessingManager
    for (int i = 0; i < 10; ++i) {
        {
            auto processing_manager = std::make_unique<ProcessingManager>(nullptr, nullptr, 0.001f);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } // ProcessingManager destructor should clean up here
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 3.0))
        << "ProcessingManager lifecycle caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}

TEST_F(ProcessingMemoryTest, ProcessingManagerCallbacks) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test ProcessingManager with callback operations
    {
        auto processing_manager = std::make_unique<ProcessingManager>(nullptr, nullptr, 0.001f);
        
        // Set up callback
        processing_manager->setWorldFrameCallback([](const WorldFrame& frame) {
            // Simple callback that doesn't retain frame data
            (void)frame;
        });
        
        // Simulate operation with callbacks
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 3.0))
        << "ProcessingManager callback operations caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}

TEST_F(ProcessingMemoryTest, ExtendedProcessingOperation) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    auto processing_manager = std::make_unique<ProcessingManager>(nullptr, nullptr, 0.001f);
    
    size_t start_memory = MemoryUtils::getCurrentRSS();
    
    // Extended operation simulation
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    size_t end_memory = MemoryUtils::getCurrentRSS();
    
    // Memory should be stable during extended operation
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(start_memory, end_memory, 10.0))
        << "Extended processing operation caused memory growth from " 
        << start_memory << " to " << end_memory << " bytes";
    
    processing_manager.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    size_t final_memory = MemoryUtils::getCurrentRSS();
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, final_memory, 5.0))
        << "Extended processing test caused overall memory growth from " 
        << pre_test_memory << " to " << final_memory << " bytes";
}

TEST_F(ProcessingMemoryTest, RapidCreateDestroy) {
    size_t pre_test_memory = MemoryUtils::getCurrentRSS();
    
    // Test rapid create/destroy to stress memory allocation/deallocation
    for (int cycle = 0; cycle < 50; ++cycle) {
        {
            auto processing_manager = std::make_unique<ProcessingManager>(
                nullptr, nullptr, 0.001f + (cycle % 10) * 0.0001f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        
        if (cycle % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    size_t post_test_memory = MemoryUtils::getCurrentRSS();
    
    EXPECT_TRUE(MemoryUtils::checkMemoryGrowth(pre_test_memory, post_test_memory, 8.0))
        << "Rapid create/destroy operations caused memory growth from " 
        << pre_test_memory << " to " << post_test_memory << " bytes";
}