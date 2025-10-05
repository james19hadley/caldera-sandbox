#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <limits>
#include <cmath>
#include <algorithm>
#include <unordered_map>

#ifdef _MSC_VER
#define FUSION_LIKELY(x) (x)
#define FUSION_UNLIKELY(x) (x)
#else
#define FUSION_LIKELY(x) (__builtin_expect(!!(x),1))
#define FUSION_UNLIKELY(x) (__builtin_expect(!!(x),0))
#endif

namespace caldera::backend::processing {

struct FusionInputLayer {
    std::string sensorId;            // logical sensor id
    // Raw pointers kept for legacy path; internal copy now taken on addLayer to avoid lifetime issues.
    const float* heights = nullptr;  // external height map pointer (size = width*height)
    const float* confidence = nullptr; // external confidence pointer
    int width = 0;
    int height = 0;
};

/**
 * Phase 0 FusionAccumulator scaffold:
 *  - Collects zero or more sensor layers per frame
 *  - For single layer: passthrough copy
 *  - For >1 layers (not implemented yet): placeholder min-z strategy (TODO Phase 1)
 *  - No dynamic allocations per frame except potential first-time reserve
 */
class FusionAccumulator {
public:
    struct FusionStats {
        size_t layerCount = 0;
        size_t activeLayerCount = 0;            // layers seen this frame (non-stale)
        size_t staleExcludedCount = 0;          // number of known sensors considered stale this frame
        std::vector<uint32_t> layerValidCounts; // per-layer count of finite values
        uint32_t fusedValidCount = 0;           // finite values in fused output
        float fusedValidRatio = 0.0f;           // fusedValidCount / totalPixels
        uint32_t fallbackMinZCount = 0;         // pixels where weights sum zero but at least one finite
        uint32_t fallbackEmptyCount = 0;        // pixels all invalid
        int strategy = 0;                       // 0=min-z,1=confidence-weight
    };

    void beginFrame(uint64_t frameId, int width, int height) {
        frameId_ = frameId;
        width_ = width;
        height_ = height;
        layers_.clear();
        framePixelCount_ = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        stats_ = FusionStats{}; // reset
        // Refresh dropout window (cheap getenv read) allowing dynamic tuning in tests
        if(!dropoutWindowLoaded_){
            const char* env = std::getenv("CALDERA_FUSION_DROPOUT_WINDOW");
            if(env){ try { dropoutWindow_ = static_cast<uint64_t>(std::stoull(env)); } catch(...){} }
            dropoutWindowLoaded_ = true; // still allow override via forceSet if needed later
        }
        // Reserve enough for at least one layer; multi-layer appends will extend but reuse capacity across frames.
        if(heightsStorage_.capacity() < framePixelCount_) heightsStorage_.reserve(framePixelCount_ * 2); // small headroom
        if(confidenceStorage_.capacity() < framePixelCount_) confidenceStorage_.reserve(framePixelCount_ * 2);
        // IMPORTANT: clear (not shrink) so offsets for this frame start at 0 while retaining capacity for reuse.
        heightsStorage_.clear();
        confidenceStorage_.clear();
    }

    // Proactive capacity reservation to eliminate allocator growth during high-throughput tests.
    void reserveFor(int width, int height, int expectedMaxLayers = 2){
        if(width<=0 || height<=0) return;
        size_t perLayer = static_cast<size_t>(width) * static_cast<size_t>(height);
        size_t target = perLayer * static_cast<size_t>(expectedMaxLayers);
        // Reserve some headroom (x2) to accommodate strategy changes without further growth.
        if(heightsStorage_.capacity() < target) heightsStorage_.reserve(target);
        if(confidenceStorage_.capacity() < target) confidenceStorage_.reserve(target);
    }

    void addLayer(const FusionInputLayer& layer) {
        if (!layer.heights || layer.width != width_ || layer.height != height_) return;
        LayerEntry entry;
        entry.sensorId = layer.sensorId;
        entry.offset = heightsStorage_.size();
        entry.hasConfidence = (layer.confidence != nullptr);
        // Append heights
        heightsStorage_.insert(heightsStorage_.end(), layer.heights, layer.heights + framePixelCount_);
        // Count valid values directly from source to avoid re-reading appended region later if we refactor
        uint32_t validCount = 0;
        for (size_t i=0;i<framePixelCount_; ++i){ float v = layer.heights[i]; if(std::isfinite(v)) ++validCount; }
        if(entry.hasConfidence){
            entry.confOffset = confidenceStorage_.size();
            confidenceStorage_.insert(confidenceStorage_.end(), layer.confidence, layer.confidence + framePixelCount_);
        }
        layers_.push_back(entry);
        stats_.layerValidCounts.push_back(validCount);
        stats_.layerCount = layers_.size();
        // Update last-seen tracking for dropout logic
        lastSeenFrameId_[entry.sensorId] = frameId_;
    }

    // weightsPerLayer: optional external array of size layerCount (pre-normalized or raw);
    // perPixelWeights: optional flat array (layerCount * totalPixels) for future fine-grained weighting (unused Phase 1).
    void fuse(std::vector<float>& outHeightMap,
              std::vector<float>* outConfidence = nullptr,
              const float* = nullptr,
              const float* = nullptr) {
        // MVP: passthrough for 1 layer, concat along width for 2 layers, else error. No weights, dropout, NaN, etc.
        // TODO: advanced fusion, dropout, weights, NaN/invalid, confidence, metrics, etc. (see plan)
        if (layers_.empty()) {
            outHeightMap.clear();
            if (outConfidence) outConfidence->clear();
            return;
        }
        if (layers_.size() == 1) {
            // Passthrough
            const LayerEntry& L = layers_[0];
            const float* hSrc = heightsStorage_.data() + L.offset;
            outHeightMap.assign(hSrc, hSrc + framePixelCount_);
            if (outConfidence) outConfidence->clear(); // not supported
            return;
        }
        if (layers_.size() == 2) {
            // Concat along width: output shape 2W x H
            const LayerEntry& L1 = layers_[0];
            const LayerEntry& L2 = layers_[1];
            if (width_ <= 0 || height_ <= 0) { outHeightMap.clear(); if (outConfidence) outConfidence->clear(); return; }
            size_t N = static_cast<size_t>(width_) * static_cast<size_t>(height_);
            const float* h1 = heightsStorage_.data() + L1.offset;
            const float* h2 = heightsStorage_.data() + L2.offset;
            outHeightMap.resize(N * 2);
            for (int y = 0; y < height_; ++y) {
                // left: h1, right: h2
                std::copy(h1 + y * width_, h1 + (y + 1) * width_, outHeightMap.begin() + y * 2 * width_);
                std::copy(h2 + y * width_, h2 + (y + 1) * width_, outHeightMap.begin() + y * 2 * width_ + width_);
            }
            if (outConfidence) outConfidence->clear(); // not supported
            return;
        }
        // Not supported
        outHeightMap.clear();
        if (outConfidence) outConfidence->clear();
        // TODO: error/exception/logging for >2 sensors
    }

    size_t layerCount() const { return layers_.size(); }
    uint64_t frameId() const { return frameId_; }
    const FusionStats& stats() const { return stats_; }

private:
    uint64_t frameId_ = 0;
    int width_ = 0;
    int height_ = 0;
    struct LayerEntry {
        std::string sensorId;
        size_t offset = 0;      // heights offset
        size_t confOffset = 0;   // confidence offset
        bool hasConfidence = false;
    };
    std::vector<LayerEntry> layers_;
    std::vector<float> heightsStorage_;
    std::vector<float> confidenceStorage_;
    size_t framePixelCount_ = 0;
    FusionStats stats_{};
    // Dropout tracking
    std::unordered_map<std::string,uint64_t> lastSeenFrameId_;
    uint64_t dropoutWindow_ = 60; // frames
    bool dropoutWindowLoaded_ = false;
};

} // namespace caldera::backend::processing
