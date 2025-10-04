/*
 * FastGaussianBlur unit tests
 */

#include <gtest/gtest.h>
#include "processing/FastGaussianBlur.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>

using namespace caldera::backend::processing;

class FastGaussianBlurTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test data with known pattern
        testWidth = 32;
        testHeight = 24;
        testData.resize(testWidth * testHeight);
        originalData.resize(testWidth * testHeight);
        
        // Generate checkerboard pattern for high-frequency content
        for (int y = 0; y < testHeight; y++) {
            for (int x = 0; x < testWidth; x++) {
                float value = ((x / 4) % 2) ^ ((y / 4) % 2) ? 1.0f : 0.0f;
                testData[y * testWidth + x] = value;
                originalData[y * testWidth + x] = value;
            }
        }
    }
    
    // Helper to calculate variance
    float calculateVariance(const std::vector<float>& data) {
        float sum = 0.0f;
        for (float val : data) sum += val;
        float mean = sum / data.size();
        
        float variance = 0.0f;
        for (float val : data) {
            float diff = val - mean;
            variance += diff * diff;
        }
        return variance / data.size();
    }
    
    int testWidth, testHeight;
    std::vector<float> testData;
    std::vector<float> originalData;
};

TEST_F(FastGaussianBlurTest, BasicFunctionality) {
    FastGaussianBlur blur(1.5f);
    
    // Apply blur
    blur.apply(testData, testWidth, testHeight);
    
    // Verify data wasn't corrupted (same size)
    EXPECT_EQ(testData.size(), originalData.size());
    
    // Verify blur reduces variance (smoothing effect)
    float originalVariance = calculateVariance(originalData);
    float blurredVariance = calculateVariance(testData);
    
    EXPECT_LT(blurredVariance, originalVariance) << "Blur should reduce high-frequency content";
}

TEST_F(FastGaussianBlurTest, PreservesDataRange) {
    FastGaussianBlur blur(2.0f);
    
    // Find original min/max
    float originalMin = *std::min_element(originalData.begin(), originalData.end());
    float originalMax = *std::max_element(originalData.begin(), originalData.end());
    
    blur.apply(testData, testWidth, testHeight);
    
    // Find blurred min/max
    float blurredMin = *std::min_element(testData.begin(), testData.end());
    float blurredMax = *std::max_element(testData.begin(), testData.end());
    
    // Blur should not create values outside original range
    EXPECT_GE(blurredMin, originalMin - 1e-6f);
    EXPECT_LE(blurredMax, originalMax + 1e-6f);
}

TEST_F(FastGaussianBlurTest, DifferentSigmaValues) {
    std::vector<float> lightBlur = testData;
    std::vector<float> heavyBlur = testData;
    
    FastGaussianBlur lightFilter(0.5f);
    FastGaussianBlur heavyFilter(3.0f);
    
    lightFilter.apply(lightBlur, testWidth, testHeight);
    heavyFilter.apply(heavyBlur, testWidth, testHeight);
    
    // Higher sigma should produce more smoothing
    float lightVariance = calculateVariance(lightBlur);
    float heavyVariance = calculateVariance(heavyBlur);
    
    EXPECT_LT(heavyVariance, lightVariance) << "Higher sigma should produce more smoothing";
}

TEST_F(FastGaussianBlurTest, HandlesEdgeCases) {
    FastGaussianBlur blur(1.0f);
    
    // Empty data should not crash
    std::vector<float> emptyData;
    EXPECT_NO_THROW(blur.apply(emptyData, 0, 0));
    
    // Single pixel
    std::vector<float> singlePixel = {42.0f};
    EXPECT_NO_THROW(blur.apply(singlePixel, 1, 1));
    EXPECT_FLOAT_EQ(singlePixel[0], 42.0f) << "Single pixel should be unchanged";
    
    // Wrong size data
    std::vector<float> wrongSize = {1.0f, 2.0f};
    EXPECT_NO_THROW(blur.apply(wrongSize, 10, 10)); // Should be no-op
}

TEST_F(FastGaussianBlurTest, ConstantInputProducesConstantOutput) {
    // Fill with constant value
    std::fill(testData.begin(), testData.end(), 5.0f);
    
    FastGaussianBlur blur(2.0f);
    blur.apply(testData, testWidth, testHeight);
    
    // All values should remain the same
    for (float val : testData) {
        EXPECT_FLOAT_EQ(val, 5.0f) << "Constant input should produce constant output";
    }
}

TEST_F(FastGaussianBlurTest, GradientTest) {
    // Create gradient from 0 to 1
    for (int i = 0; i < testWidth * testHeight; i++) {
        testData[i] = static_cast<float>(i) / (testWidth * testHeight - 1);
    }
    
    FastGaussianBlur blur(1.0f);
    blur.apply(testData, testWidth, testHeight);
    
    // Gradient should be preserved (monotonic)
    bool isMonotonic = true;
    for (int y = 0; y < testHeight - 1 && isMonotonic; y++) {
        for (int x = 0; x < testWidth - 1 && isMonotonic; x++) {
            int curr = y * testWidth + x;
            int next = curr + 1;
            isMonotonic = testData[curr] <= testData[next] + 1e-4f; // Allow small tolerance
        }
    }
    
    EXPECT_TRUE(isMonotonic) << "Blur should preserve gradient monotonicity";
}

TEST_F(FastGaussianBlurTest, PerformanceCharacteristics) {
#ifdef NDEBUG
    // Performance test only runs in Release mode
    // Test with large data for performance
    const size_t largeWidth = 640;
    const size_t largeHeight = 480;
    std::vector<float> largeData(largeWidth * largeHeight, 100.0f);
    
    // Add some variation
    for (size_t i = 0; i < largeData.size(); i += 100) {
        largeData[i] = 200.0f;
    }
    
    FastGaussianBlur blur(2.0f);
    
    // Measure multiple blur operations
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10; i++) {
        blur.apply(largeData, largeWidth, largeHeight);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto avgTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 10;
    
    // Should be much faster than naive O(n*r²) implementation
    // For 640x480, should complete in under 10ms (10000 μs) on modern hardware
    EXPECT_LT(avgTime, 10000) << "Blur should be fast enough for real-time processing";
#else
    // Skip performance test in Debug mode - optimization disabled
    GTEST_SKIP() << "Performance test skipped in Debug build (no optimization)";
#endif
}

TEST_F(FastGaussianBlurTest, SymmetryTest) {
    // Create symmetric pattern
    std::fill(testData.begin(), testData.end(), 0.0f);
    
    // Put a bright spot in the center
    int centerX = testWidth / 2;
    int centerY = testHeight / 2;
    testData[centerY * testWidth + centerX] = 1.0f;
    
    FastGaussianBlur blur(2.0f);
    blur.apply(testData, testWidth, testHeight);
    
    // Check symmetry around center
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int x1 = centerX + dx, y1 = centerY + dy;
            int x2 = centerX - dx, y2 = centerY - dy;
            
            if (x1 >= 0 && x1 < testWidth && y1 >= 0 && y1 < testHeight &&
                x2 >= 0 && x2 < testWidth && y2 >= 0 && y2 < testHeight) {
                
                float val1 = testData[y1 * testWidth + x1];
                float val2 = testData[y2 * testWidth + x2];
                
                EXPECT_NEAR(val1, val2, 1e-5f) << "Blur should be symmetric around center";
            }
        }
    }
}

TEST_F(FastGaussianBlurTest, IntegrationWithProcessingManager) {
    // Test that it works as IHeightMapFilter
    std::shared_ptr<IHeightMapFilter> filter = std::make_shared<FastGaussianBlur>(1.5f);
    
    EXPECT_NO_THROW(filter->apply(testData, testWidth, testHeight));
    
    // Should behave the same as direct usage
    std::vector<float> directBlur = originalData;
    FastGaussianBlur directFilter(1.5f);
    directFilter.apply(directBlur, testWidth, testHeight);
    
    for (size_t i = 0; i < testData.size(); i++) {
        EXPECT_FLOAT_EQ(testData[i], directBlur[i]) << "Interface and direct usage should match";
    }
}