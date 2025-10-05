/***********************************************************************
TemporalFilter.cpp - Implementation of temporal filtering algorithm
based on SARndbox FrameFilter for stability detection and jitter elimination.
***********************************************************************/

#include "TemporalFilter.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>

namespace caldera {
namespace backend {
namespace processing {

constexpr uint16_t INVALID_DEPTH = 2048U;  // Invalid depth marker (matches SARndbox)
constexpr float HEIGHT_SCALE = 1000.0f;    // Convert meters to mm for buffer storage

TemporalFilter::TemporalFilter() : config_() {
}

TemporalFilter::TemporalFilter(const FilterConfig& config) 
    : config_(config) {
}

void TemporalFilter::initialize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    
    const size_t numPixels = width * height;
    
    // Allocate circular averaging buffer
    // Each slot stores one frame worth of depth values
    averagingBuffer_.resize(numPixels * config_.numAveragingSlots, INVALID_DEPTH);
    averagingSlotIndex_ = 0;
    
    // Allocate per-pixel statistics
    pixelStats_.resize(numPixels);
    
    // Allocate valid value buffer  
    validBuffer_.resize(numPixels, 0.0f);
    
    // Reset counters
    frameCount_ = 0;
    stablePixelCount_ = 0;
    unstablePixelCount_ = 0;
    
    std::cout << "TemporalFilter initialized for " << width << "x" << height 
              << " with " << config_.numAveragingSlots << " averaging slots" << std::endl;
}

void TemporalFilter::apply(std::vector<float>& data, int width, int height) {
    // Initialize if dimensions changed
    if (uint32_t(width) != width_ || uint32_t(height) != height_) {
        initialize(width, height);
    }
    
    if (data.empty() || width_ == 0 || height_ == 0) {
        return; // Pass-through if not initialized
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Reset frame statistics
    stablePixelCount_ = 0;
    unstablePixelCount_ = 0;
    
    // Process each pixel (based on SARndbox main filtering loop)  
    const size_t numPixels = std::min(data.size(), size_t(width_ * height_));
    
    for (size_t i = 0; i < numPixels; ++i) {
        float inputHeight = data[i];
        
        // Skip invalid input pixels (NaN or infinity)
        if (!std::isfinite(inputHeight)) {
            data[i] = validBuffer_[i]; // Use last valid value
            continue;
        }
        
        // Get current and new values for this pixel
        const size_t bufferIndex = i + averagingSlotIndex_ * numPixels;
        const uint16_t oldBufferValue = averagingBuffer_[bufferIndex];
        const uint16_t newBufferValue = heightToBuffer(inputHeight);
        
        // Store new value in circular buffer
        averagingBuffer_[bufferIndex] = newBufferValue;
        
        // Update pixel statistics (matches SARndbox statistics update)
        updatePixelStatistics(i, inputHeight, oldBufferValue);
        
        // Check stability and apply hysteresis
        auto& stats = pixelStats_[i];
        
        
        if (stats.checkStability(config_)) {
            // Pixel is stable - apply hysteresis logic
            float newFilteredValue = stats.getMean();
            float outputValue = applyHysteresis(i, newFilteredValue);
            
            data[i] = outputValue;
            validBuffer_[i] = outputValue;
            stats.lastValidValue = outputValue;
            stats.isStable = true;
            stablePixelCount_++;
        } else {
            // Pixel is unstable
            if (config_.retainValids) {
                // Keep previous stable value
                data[i] = validBuffer_[i];
            } else {
                // Use default unstable value
                data[i] = config_.instableValue;
                validBuffer_[i] = config_.instableValue;
            }
            stats.isStable = false;
            unstablePixelCount_++;
        }
    }
    
    // Advance to next averaging slot (circular buffer)
    if (++averagingSlotIndex_ >= config_.numAveragingSlots) {
        averagingSlotIndex_ = 0;
    }
    
    frameCount_++;
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    // Log statistics periodically (every 30 frames = ~1 second at 30fps)
    if (frameCount_ % 30 == 0) {
        float stabilityRatio = float(stablePixelCount_) / float(stablePixelCount_ + unstablePixelCount_);
        std::cout << "TemporalFilter frame " << frameCount_ 
                  << ": " << stablePixelCount_ << " stable, " << unstablePixelCount_ << " unstable"
                  << " (stability: " << (stabilityRatio * 100.0f) << "%)"
                  << " processing: " << (duration.count() / 1000.0f) << "ms" << std::endl;
    }
}

void TemporalFilter::updatePixelStatistics(uint32_t pixelIndex, float newValue, uint16_t oldBufferValue) {
    auto& stats = pixelStats_[pixelIndex];
    
    // Convert height to buffer format for statistics (matches SARndbox)
    uint64_t newBufferValue = static_cast<uint64_t>(heightToBuffer(newValue));
    
    // Add new sample to statistics
    stats.numSamples++;
    stats.sumSamples += newBufferValue;
    stats.sumSquares += newBufferValue * newBufferValue;
    
    // Remove old sample if it was valid (matches SARndbox logic)
    if (oldBufferValue != INVALID_DEPTH) {
        uint64_t oldValue = static_cast<uint64_t>(oldBufferValue);
        stats.numSamples--;
        stats.sumSamples -= oldValue;
        stats.sumSquares -= oldValue * oldValue;
    }
    
    // Ensure non-negative values (shouldn't happen, but safety check)
    if (stats.numSamples == 0) {
        stats.sumSamples = 0;
        stats.sumSquares = 0;
    }
}

float TemporalFilter::applyHysteresis(uint32_t pixelIndex, float newFilteredValue) {
    const auto& stats = pixelStats_[pixelIndex];
    
    // Convert to buffer format for comparison (hysteresis threshold is in mm)
    float newBufferValue = newFilteredValue * HEIGHT_SCALE; // Convert to mm
    float lastBufferValue = stats.lastValidValue * HEIGHT_SCALE; // Convert to mm
    
    // Apply hysteresis: only update if change exceeds threshold in buffer units
    // (matches SARndbox hysteresis logic)
    if (std::abs(newBufferValue - lastBufferValue) >= config_.hysteresis) {
        return newFilteredValue;
    } else {
        return stats.lastValidValue; // Keep previous value
    }
}

uint16_t TemporalFilter::heightToBuffer(float height) const {
    // Convert height (meters) to buffer format (mm, clamped to uint16 range)
    float heightMm = height * HEIGHT_SCALE;
    
    // Clamp to valid range (avoid INVALID_DEPTH value)
    if (heightMm < 0.0f) return 0;
    if (heightMm >= float(INVALID_DEPTH)) return INVALID_DEPTH - 1;
    
    return static_cast<uint16_t>(heightMm);
}

float TemporalFilter::bufferToHeight(uint16_t buffer) const {
    if (buffer == INVALID_DEPTH) return 0.0f;
    return float(buffer) / HEIGHT_SCALE;
}

void TemporalFilter::updateConfig(const FilterConfig& config) {
    config_ = config;
    
    // If buffer size changed, need to reinitialize  
    if (width_ > 0 && height_ > 0) {
        const size_t currentSlots = averagingBuffer_.size() / (width_ * height_);
        if (currentSlots != config_.numAveragingSlots) {
            std::cout << "TemporalFilter: Reinitializing due to buffer size change ("
                      << currentSlots << " -> " << config_.numAveragingSlots << " slots)" << std::endl;
            initialize(width_, height_);
        }
    }
}

TemporalFilter::FilterStatistics TemporalFilter::getStatistics() const {
    FilterStatistics stats;
    stats.totalFrames = frameCount_;
    stats.stablePixels = stablePixelCount_;
    stats.unstablePixels = unstablePixelCount_;
    
    const uint32_t totalPixels = stablePixelCount_ + unstablePixelCount_;
    if (totalPixels > 0) {
        stats.stabilityRatio = float(stablePixelCount_) / float(totalPixels);
    }
    
    // Calculate average variance across all pixels
    float totalVariance = 0.0f;
    uint32_t validVariances = 0;
    for (const auto& pixelStat : pixelStats_) {
        if (pixelStat.numSamples > 1) {
            totalVariance += pixelStat.getVariance();
            validVariances++;
        }
    }
    
    if (validVariances > 0) {
        stats.avgVariance = totalVariance / float(validVariances);
    }
    
    return stats;
}

void TemporalFilter::reset() {
    // Clear all buffers
    std::fill(averagingBuffer_.begin(), averagingBuffer_.end(), INVALID_DEPTH);
    std::fill(validBuffer_.begin(), validBuffer_.end(), 0.0f);
    
    // Reset pixel statistics
    for (auto& stat : pixelStats_) {
        stat = PixelStatistics{};
    }
    
    // Reset indices and counters
    averagingSlotIndex_ = 0;
    frameCount_ = 0;
    stablePixelCount_ = 0;
    unstablePixelCount_ = 0;
    
    std::cout << "TemporalFilter: Reset all buffers and statistics" << std::endl;
}

void TemporalFilter::processFrame(const InternalPointCloud& input, InternalPointCloud& output) {
    if (input.points.empty()) {
        output = input; // Pass-through if empty
        return;
    }
    
    // Convert InternalPointCloud to height map
    std::vector<float> heightMap;
    heightMap.reserve(input.points.size());
    
    for (const auto& point : input.points) {
        if (point.valid && std::isfinite(point.z)) {
            heightMap.push_back(point.z);
        } else {
            heightMap.push_back(std::numeric_limits<float>::quiet_NaN());
        }
    }
    
    // Apply temporal filtering
    apply(heightMap, input.width, input.height);
    
    // Convert back to InternalPointCloud
    output = input; // Copy structure
    for (size_t i = 0; i < std::min(heightMap.size(), output.points.size()); ++i) {
        output.points[i].z = heightMap[i];
        output.points[i].valid = std::isfinite(heightMap[i]);
    }
}

}}} // namespace caldera::backend::processing