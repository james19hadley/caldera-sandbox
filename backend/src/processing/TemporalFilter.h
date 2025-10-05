/***********************************************************************
TemporalFilter.h - Class implementing statistical temporal filtering 
based on SARndbox FrameFilter algorithm for stability detection and
multi-frame averaging to eliminate jittering in static regions.
***********************************************************************/

#pragma once

#include "IHeightMapFilter.h"
#include "ProcessingTypes.h"
#include <vector>
#include <memory>

namespace caldera {
namespace backend {
namespace processing {

/**
 * TemporalFilter implements temporal stability filtering based on SARndbox's 
 * FrameFilter algorithm. It maintains per-pixel statistics across multiple 
 * frames to detect stable regions and applies hysteresis to prevent oscillation.
 * 
 * Key features:
 * - Multi-frame averaging with circular buffer (configurable depth)
 * - Per-pixel mean/variance calculation for stability detection  
 * - Hysteresis filtering to prevent rapid state changes
 * - Configurable parameters for different sand behaviors
 */
class TemporalFilter : public caldera::backend::processing::IHeightMapFilter {
public:
    /**
     * Configuration parameters for temporal filtering
     */
    struct FilterConfig {
        // Averaging parameters
        uint32_t numAveragingSlots = 30;        // Number of frames in circular buffer
        uint32_t minNumSamples = 10;            // Min samples needed for stability
        
        // Stability detection  
        float maxVariance = 1000000.0f;         // Max variance for stable pixel (buffer units = mm²)
        float hysteresis = 500.0f;              // Hysteresis threshold (buffer units = mm)
        
        // Update rates (exponential moving average factors)
        float stableUpdateRate = 0.05f;         // α for stable pixels (slow)
        float unstableUpdateRate = 0.5f;        // α for unstable pixels (fast)
        
        // Behavior flags
        bool retainValids = true;               // Keep previous stable values for unstable pixels
        float instableValue = 0.0f;             // Default value for unstable pixels if !retainValids
        
        // Performance optimization
        bool enableSpatialFilter = false;       // Apply additional spatial smoothing
    };
    
    /**
     * Per-pixel statistics structure (based on SARndbox statBuffer format)
     */
    struct PixelStatistics {
        uint32_t numSamples = 0;        // Number of valid samples in buffer
        uint64_t sumSamples = 0;        // Sum of all samples (for mean calculation)  
        uint64_t sumSquares = 0;        // Sum of squares (for variance calculation)
        float lastValidValue = 0.0f;    // Most recent stable output value
        bool isStable = false;          // Current stability state (hysteresis controlled)
        
        // Calculate running mean (convert back to meters from buffer format)
        float getMean() const {
            if (numSamples == 0) return 0.0f;
            float meanBuffer = float(sumSamples) / float(numSamples);
            return meanBuffer / 1000.0f; // Convert mm back to meters
        }
        
        // Calculate variance for stability detection (in buffer units = mm²)
        float getVariance() const {
            if (numSamples <= 1) return 1000000.0f; // High variance for insufficient samples
            
            // Use double precision to avoid overflow with large buffer values
            double meanBuffer = double(sumSamples) / double(numSamples);
            double variance = (double(sumSquares) / double(numSamples)) - (meanBuffer * meanBuffer);
            
            return std::max(0.0, variance); // Ensure non-negative variance
        }
        
        // Check if pixel meets stability criteria
        bool checkStability(const FilterConfig& config) const {
            return numSamples >= config.minNumSamples && 
                   getVariance() <= config.maxVariance;
        }
    };

private:
    // Configuration
    FilterConfig config_;
    
    // Frame dimensions
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    
    // Circular averaging buffer (based on SARndbox averagingBuffer)
    // Format: [slot0_pixels][slot1_pixels]...[slotN_pixels]
    std::vector<uint16_t> averagingBuffer_;
    uint32_t averagingSlotIndex_ = 0;
    
    // Per-pixel statistics buffer (based on SARndbox statBuffer)  
    // Format: for each pixel: [numSamples, sumSamples, sumSquares]
    std::vector<PixelStatistics> pixelStats_;
    
    // Output stability buffer (most recent valid values)
    std::vector<float> validBuffer_;
    
    // Processing statistics
    uint64_t frameCount_ = 0;
    uint32_t stablePixelCount_ = 0;
    uint32_t unstablePixelCount_ = 0;

public:
    /**
     * Constructor
     */
    TemporalFilter();
    TemporalFilter(const FilterConfig& config);
    
    /**
     * Destructor 
     */
    virtual ~TemporalFilter() = default;
    
    /**
     * Initialize filter for specific frame dimensions
     */
    void initialize(uint32_t width, uint32_t height);
    
    /**
     * Apply temporal filtering to height map data (IHeightMapFilter interface)
     * Based on SARndbox filterThreadMethod algorithm
     */
    void apply(std::vector<float>& data, int width, int height) override;
    
    /**
     * Process a point cloud through temporal filtering (alternative interface)
     * Based on SARndbox filterThreadMethod algorithm  
     */
    void processFrame(const InternalPointCloud& input, InternalPointCloud& output);
    
    /**
     * Configuration management
     */
    void updateConfig(const FilterConfig& config);
    const FilterConfig& getConfig() const { return config_; }
    
    /**
     * Statistics and monitoring
     */
    struct FilterStatistics {
        uint64_t totalFrames = 0;
        uint32_t stablePixels = 0;
        uint32_t unstablePixels = 0;
        float stabilityRatio = 0.0f;        // Percentage of stable pixels
        float avgVariance = 0.0f;           // Average variance across all pixels
        float processingTimeMs = 0.0f;      // Last frame processing time
    };
    
    FilterStatistics getStatistics() const;
    
    /**
     * Reset filter state (clear all buffers)
     */
    void reset();
    
    /**
     * Debug and visualization support
     */
    const std::vector<PixelStatistics>& getPixelStatistics() const { return pixelStats_; }
    const std::vector<float>& getValidBuffer() const { return validBuffer_; }
    
private:
    /**
     * Update pixel statistics with new sample (based on SARndbox algorithm)
     */
    void updatePixelStatistics(uint32_t pixelIndex, float newValue, uint16_t oldBufferValue);
    
    /**
     * Apply hysteresis logic to determine output value
     */
    float applyHysteresis(uint32_t pixelIndex, float newFilteredValue);
    
    /**
     * Convert height value to internal buffer format (matches SARndbox RawDepth)
     */
    uint16_t heightToBuffer(float height) const;
    
    /**
     * Convert buffer value back to height (with validation)
     */  
    float bufferToHeight(uint16_t buffer) const;
};

}}} // namespace caldera::backend::processing