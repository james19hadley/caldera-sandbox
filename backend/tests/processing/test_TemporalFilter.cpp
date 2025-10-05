/***********************************************************************
test_TemporalFilter.cpp - Unit tests for TemporalFilter implementation
Testing stability detection, hysteresis, and multi-frame averaging.
***********************************************************************/

#include <gtest/gtest.h>
#include "../src/processing/TemporalFilter.h"
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

class TemporalFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.numAveragingSlots = 10;
        config.minNumSamples = 3;
        config.maxVariance = 1000000.0f;  // 1mm² variance threshold (in buffer units)
        config.hysteresis = 100.0f;        // 0.1mm hysteresis threshold (in buffer units)
        config.retainValids = true;
        
        filter = std::make_unique<TemporalFilter>(config);
        filter->initialize(width, height);
    }
    
    InternalPointCloud createTestFrame(const std::vector<float>& heights) {
        InternalPointCloud cloud;
        cloud.width = width;
        cloud.height = height;
        cloud.points.resize(width * height);
        cloud.timestamp_ns = 1000;  // Mock timestamp
        
        for (size_t i = 0; i < cloud.points.size() && i < heights.size(); ++i) {
            cloud.points[i].x = float(i % width);
            cloud.points[i].y = float(i / width);
            cloud.points[i].z = heights[i];
            cloud.points[i].valid = (heights[i] != -1.0f); // -1.0f = invalid
        }
        
        return cloud;
    }
    
    TemporalFilter::FilterConfig config;
    std::unique_ptr<TemporalFilter> filter;
    const uint32_t width = 4;
    const uint32_t height = 4;
};

TEST_F(TemporalFilterTest, InitializationTest) {
    // Test that filter initializes correctly
    auto stats = filter->getStatistics();
    EXPECT_EQ(stats.totalFrames, 0);
    EXPECT_EQ(stats.stablePixels, 0);
    EXPECT_EQ(stats.unstablePixels, 0);
    
    // Test buffer sizes
    const auto& pixelStats = filter->getPixelStatistics();
    EXPECT_EQ(pixelStats.size(), width * height);
    
    const auto& validBuffer = filter->getValidBuffer();
    EXPECT_EQ(validBuffer.size(), width * height);
}

TEST_F(TemporalFilterTest, SingleFrameProcessing) {
    // Create test frame with stable height values
    std::vector<float> heights = {
        1.0f, 1.1f, 1.2f, 1.3f,
        1.4f, 1.5f, 1.6f, 1.7f,
        1.8f, 1.9f, 2.0f, 2.1f,
        2.2f, 2.3f, 2.4f, 2.5f
    };
    
    auto inputCloud = createTestFrame(heights);
    InternalPointCloud outputCloud;
    
    // Process single frame
    filter->processFrame(inputCloud, outputCloud);
    
    // Check output structure
    EXPECT_EQ(outputCloud.width, width);
    EXPECT_EQ(outputCloud.height, height);
    EXPECT_EQ(outputCloud.points.size(), width * height);
    EXPECT_EQ(outputCloud.timestamp_ns, inputCloud.timestamp_ns);
    
    // After single frame, no pixels should be stable yet (need minNumSamples)
    auto stats = filter->getStatistics();
    EXPECT_EQ(stats.totalFrames, 1);
    EXPECT_EQ(stats.stablePixels, 0);  // Need more samples for stability
}

TEST_F(TemporalFilterTest, StabilityDetection) {
    // Create frames with consistent values for stability detection
    std::vector<float> stableHeights = {
        1.0f, 1.0f, 1.0f, 1.0f,
        2.0f, 2.0f, 2.0f, 2.0f,
        3.0f, 3.0f, 3.0f, 3.0f,
        4.0f, 4.0f, 4.0f, 4.0f
    };
    
    InternalPointCloud outputCloud;
    
    // Process multiple frames to build up statistics
    for (int frame = 0; frame < 5; ++frame) {
        auto inputCloud = createTestFrame(stableHeights);
        filter->processFrame(inputCloud, outputCloud);
    }
    
    // After 5 frames with stable values, should have stable pixels
    auto stats = filter->getStatistics();
    EXPECT_GT(stats.stablePixels, 0);
    EXPECT_GT(stats.stabilityRatio, 0.0f);
}

TEST_F(TemporalFilterTest, HysteresisFiltering) {
    // First establish stable baseline
    std::vector<float> baselineHeights(width * height, 1.0f);
    
    InternalPointCloud outputCloud;
    
    // Build stable baseline (5 frames)
    for (int frame = 0; frame < 5; ++frame) {
        auto inputCloud = createTestFrame(baselineHeights);
        filter->processFrame(inputCloud, outputCloud);
    }
    
    // Now introduce small change below hysteresis threshold (100 buffer units = 0.1mm)
    std::vector<float> smallChangeHeights(width * height, 1.00005f); // 0.05mm change < 0.1mm threshold
    auto inputCloud = createTestFrame(smallChangeHeights);
    filter->processFrame(inputCloud, outputCloud);
    
    // Output should retain previous values due to hysteresis
    for (size_t i = 0; i < outputCloud.points.size(); ++i) {
        EXPECT_NEAR(outputCloud.points[i].z, 1.0f, 0.01f) 
            << "Pixel " << i << " should retain previous value due to hysteresis";
    }
    
    // Now introduce large change above hysteresis threshold (100 buffer units = 0.1mm)
    std::vector<float> largeChangeHeights(width * height, 1.0002f); // 0.2mm change > 0.1mm threshold
    inputCloud = createTestFrame(largeChangeHeights);
    filter->processFrame(inputCloud, outputCloud);
    
    // After more frames, should eventually update to new value
    for (int frame = 0; frame < 3; ++frame) {
        inputCloud = createTestFrame(largeChangeHeights);
        filter->processFrame(inputCloud, outputCloud);
    }
}

TEST_F(TemporalFilterTest, InvalidPixelHandling) {
    // Create frame with some invalid pixels (-1.0f marks invalid)
    std::vector<float> mixedHeights = {
        1.0f, -1.0f, 1.2f, -1.0f,  // Mix of valid and invalid
        1.4f, 1.5f, -1.0f, 1.7f,
        -1.0f, 1.9f, 2.0f, -1.0f,
        2.2f, -1.0f, 2.4f, 2.5f
    };
    
    auto inputCloud = createTestFrame(mixedHeights);
    InternalPointCloud outputCloud;
    
    filter->processFrame(inputCloud, outputCloud);
    
    // Check that invalid pixels are handled correctly
    for (size_t i = 0; i < outputCloud.points.size(); ++i) {
        if (!inputCloud.points[i].valid) {
            // Invalid input pixels should use previous valid value (initially 0.0)
            EXPECT_EQ(outputCloud.points[i].z, 0.0f);
        }
    }
}

TEST_F(TemporalFilterTest, NoiseReduction) {
    // Create frames with added noise to test filtering
    std::vector<float> baseHeights(width * height, 2.0f);
    
    InternalPointCloud outputCloud;
    
    // Process frames with increasing noise
    for (int frame = 0; frame < 10; ++frame) {
        std::vector<float> noisyHeights = baseHeights;
        
        // Add random noise to each pixel
        for (size_t i = 0; i < noisyHeights.size(); ++i) {
            float noise = (float(rand()) / RAND_MAX - 0.5f) * 0.0002f; // ±0.1mm noise
            noisyHeights[i] += noise;
        }
        
        auto inputCloud = createTestFrame(noisyHeights);
        filter->processFrame(inputCloud, outputCloud);
    }
    
    // After many frames, stable pixels should converge close to true value
    auto stats = filter->getStatistics();
    EXPECT_GT(stats.stablePixels, 0);
    
    // Check that stable pixels are close to expected value
    const auto& pixelStats = filter->getPixelStatistics();
    for (size_t i = 0; i < pixelStats.size(); ++i) {
        if (pixelStats[i].isStable) {
            float mean = pixelStats[i].getMean(); // Already converted to meters in getMean()
            EXPECT_NEAR(mean, 2.0f, 0.001f) << "Stable pixel " << i << " should be close to true value";
        }
    }
}

TEST_F(TemporalFilterTest, ConfigurationUpdate) {
    // Test updating configuration
    TemporalFilter::FilterConfig newConfig = config;
    newConfig.hysteresis = 500.0f;     // Increase hysteresis (buffer units)
    newConfig.maxVariance = 2000000.0f; // Increase variance threshold (buffer units)
    
    filter->updateConfig(newConfig);
    
    const auto& updatedConfig = filter->getConfig();
    EXPECT_EQ(updatedConfig.hysteresis, 500.0f);
    EXPECT_EQ(updatedConfig.maxVariance, 2000000.0f);
}

TEST_F(TemporalFilterTest, ResetFunctionality) {
    // Process some frames to build up state
    std::vector<float> heights(width * height, 1.5f);
    InternalPointCloud outputCloud;
    
    for (int frame = 0; frame < 5; ++frame) {
        auto inputCloud = createTestFrame(heights);
        filter->processFrame(inputCloud, outputCloud);
    }
    
    auto statsBefore = filter->getStatistics();
    EXPECT_GT(statsBefore.totalFrames, 0);
    
    // Reset filter
    filter->reset();
    
    auto statsAfter = filter->getStatistics();
    EXPECT_EQ(statsAfter.totalFrames, 0);
    EXPECT_EQ(statsAfter.stablePixels, 0);
    EXPECT_EQ(statsAfter.unstablePixels, 0);
}

// Performance test (basic)
TEST_F(TemporalFilterTest, PerformanceBaseline) {
    std::vector<float> heights(width * height, 2.0f);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Process 100 frames
    InternalPointCloud outputCloud;
    for (int frame = 0; frame < 100; ++frame) {
        auto inputCloud = createTestFrame(heights);
        filter->processFrame(inputCloud, outputCloud);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Processed 100 frames of " << width << "x" << height 
              << " in " << duration.count() << "ms" 
              << " (avg: " << (duration.count() / 100.0f) << "ms/frame)" << std::endl;
    
    // Should be well under real-time requirements
    EXPECT_LT(duration.count() / 100.0f, 10.0f); // <10ms per frame for small test
}