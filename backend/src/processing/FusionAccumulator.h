#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <limits>
#include <cmath>
#include <algorithm>

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
    }

    // weightsPerLayer: optional external array of size layerCount (pre-normalized or raw);
    // perPixelWeights: optional flat array (layerCount * totalPixels) for future fine-grained weighting (unused Phase 1).
    void fuse(std::vector<float>& outHeightMap,
              std::vector<float>* outConfidence = nullptr,
              const float* weightsPerLayer = nullptr,
              const float* perPixelWeights = nullptr) {
        (void)weightsPerLayer; (void)perPixelWeights; // unused in current phase
        const size_t total = framePixelCount_;
        if (outHeightMap.size() != total) outHeightMap.assign(total, 0.0f);
        if (outConfidence && outConfidence->size() != total) outConfidence->assign(total, 1.0f);
        if (layers_.empty()) {
            if (outConfidence) std::fill(outConfidence->begin(), outConfidence->end(), 0.0f);
            stats_.fusedValidCount = 0;
            stats_.fusedValidRatio = 0.0f;
            return;
        }
        if (layers_.size() == 1) {
            const LayerEntry& L = layers_[0];
            const float* hSrc = heightsStorage_.data() + L.offset;
            std::copy(hSrc, hSrc + total, outHeightMap.data());
            uint32_t fusedValid=0; for(size_t i=0;i<total;++i) if(std::isfinite(outHeightMap[i])) ++fusedValid;
            if (outConfidence) {
                if (L.hasConfidence) {
                    const float* cSrc = confidenceStorage_.data() + L.confOffset;
                    // Clamp values into [0,1]
                    for(size_t i=0;i<total;++i){ float cv=cSrc[i]; if(!(cv>=0.f) || !std::isfinite(cv)) cv=0.f; if(cv>1.f) cv=1.f; (*outConfidence)[i]=cv; }
                } else {
                    std::fill(outConfidence->begin(), outConfidence->end(), 1.0f);
                }
            }
            stats_.strategy = layers_[0].hasConfidence ? 1 : 0; // treat single layer w/ confidence as strategy 1 for consistency
            stats_.fusedValidCount = fusedValid;
            stats_.fusedValidRatio = total? (float)fusedValid / (float)total : 0.0f;
            return;
        }
        // Multi-layer
        bool anyConfidence = false; for(const auto& L: layers_) if(L.hasConfidence){ anyConfidence=true; break; }
        stats_.strategy = anyConfidence ? 1 : 0;
        uint32_t fusedValid=0, fbMinZ=0, fbEmpty=0;
        if(stats_.strategy == 1){
            // Weighted strategy with per-pixel fallback to min-z
            for(size_t i=0;i<total;++i){
                double sumR=0.0, sumRH=0.0; bool anyFinite=false; bool anyWeight=false;
                for(const auto& L: layers_){
                    const float* hSrc = heightsStorage_.data() + L.offset; float v=hSrc[i];
                    if(!std::isfinite(v)) continue; anyFinite=true; float c=1.0f;
                    if(L.hasConfidence){ const float* cSrc = confidenceStorage_.data() + L.confOffset; float rawC=cSrc[i]; if(!(rawC>=0.f) || !std::isfinite(rawC)) rawC=0.f; if(rawC>1.f) rawC=1.f; c=rawC; }
                    if(c>0.f){ anyWeight=true; sumR+=c; sumRH+= double(c)*double(v); }
                }
                if(!anyFinite){ outHeightMap[i]=0.0f; ++fbEmpty; continue; }
                if(!anyWeight || sumR<=0.0){
                    float best=std::numeric_limits<float>::infinity();
                    for(const auto& L: layers_){ const float* hSrc=heightsStorage_.data()+L.offset; float v=hSrc[i]; if(!std::isfinite(v)) continue; if(v<best) best=v; }
                    if(best==std::numeric_limits<float>::infinity()){ outHeightMap[i]=0.0f; ++fbEmpty; }
                    else { outHeightMap[i]=best; ++fusedValid; ++fbMinZ; }
                } else {
                    float hv = static_cast<float>(sumRH / sumR); outHeightMap[i]=hv; ++fusedValid;
                }
            }
            if(outConfidence){
                for(size_t i=0;i<total;++i){
                    double sumR=0.0, sumRC=0.0; bool anyFinite=false; for(const auto& L: layers_){ const float* hSrc=heightsStorage_.data()+L.offset; float v=hSrc[i]; if(!std::isfinite(v)) continue; anyFinite=true; float c=1.0f; if(L.hasConfidence){ const float* cSrc=confidenceStorage_.data()+L.confOffset; float rawC=cSrc[i]; if(!(rawC>=0.f)||!std::isfinite(rawC)) rawC=0.f; if(rawC>1.f) rawC=1.f; c=rawC; } if(c>0.f){ sumR+=c; sumRC+=c*c; } }
                    if(!anyFinite || sumR<=0.0){ (*outConfidence)[i]=0.0f; }
                    else { (*outConfidence)[i]= static_cast<float>(sumRC / sumR); }
                }
            }
        } else {
            // Pure min-z
            for(size_t i=0;i<total;++i){
                float best=std::numeric_limits<float>::infinity();
                for(const auto& L: layers_){ const float* hSrc=heightsStorage_.data()+L.offset; float v=hSrc[i]; if(!std::isfinite(v)) continue; if(v<best) best=v; }
                if(best==std::numeric_limits<float>::infinity()){ outHeightMap[i]=0.0f; ++fbEmpty; }
                else { outHeightMap[i]=best; ++fusedValid; }
            }
            if(outConfidence){
                for(size_t i=0;i<total;++i){
                    float cBest=0.0f; for(const auto& L: layers_){ if(!L.hasConfidence){ cBest=std::max(cBest,1.0f); continue; } const float* cSrc=confidenceStorage_.data()+L.confOffset; float cv=cSrc[i]; if(!(cv>=0.f)||!std::isfinite(cv)) cv=0.f; if(cv>1.f) cv=1.f; cBest=std::max(cBest,cv); } (*outConfidence)[i]=cBest; }
            }
        }
        stats_.fusedValidCount = fusedValid;
        stats_.fusedValidRatio = total? (float)fusedValid / (float)total : 0.0f;
        stats_.fallbackMinZCount = fbMinZ;
        stats_.fallbackEmptyCount = fbEmpty;
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
};

} // namespace caldera::backend::processing
