#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <limits>
#include <cmath>

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
    }

    void addLayer(const FusionInputLayer& layer) {
        if (!layer.heights || layer.width != width_ || layer.height != height_) return;
        LayerEntry entry;
        entry.sensorId = layer.sensorId;
        entry.offset = heightsStorage_.size();
        entry.hasConfidence = (layer.confidence != nullptr);
        size_t neededH = entry.offset + framePixelCount_;
        heightsStorage_.resize(neededH);
        std::copy(layer.heights, layer.heights + framePixelCount_, heightsStorage_.data() + entry.offset);
        // Count valid values for this layer
        uint32_t validCount = 0;
        for (size_t i=0;i<framePixelCount_;++i) {
            float v = heightsStorage_[entry.offset + i];
            if (std::isfinite(v)) ++validCount;
        }
        if (entry.hasConfidence) {
            entry.confOffset = confidenceStorage_.size();
            confidenceStorage_.resize(entry.confOffset + framePixelCount_);
            std::copy(layer.confidence, layer.confidence + framePixelCount_, confidenceStorage_.data() + entry.confOffset);
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
                    std::copy(cSrc, cSrc + total, outConfidence->data());
                } else {
                    std::fill(outConfidence->begin(), outConfidence->end(), 1.0f);
                }
            }
            stats_.fusedValidCount = fusedValid;
            stats_.fusedValidRatio = total? (float)fusedValid / (float)total : 0.0f;
            return;
        }
            bool canConfidenceWeight = true;
            // If no layer provides confidence, we still can weight but all defaults => degenerates to mean/min decision; choose min-z.
            bool anyConfidence = false;
            for (auto& L : layers_) if (L.hasConfidence) { anyConfidence = true; break; }
            if (!anyConfidence) canConfidenceWeight = false;
            if (!canConfidenceWeight) stats_.strategy = 0; else stats_.strategy = 1;
            uint32_t fusedValid=0; uint32_t fbMinZ=0; uint32_t fbEmpty=0;
            if (stats_.strategy == 1) {
                // Confidence-weighted
                for (size_t i=0;i<total;++i) {
                    double sumR = 0.0; double sumRH = 0.0; bool anyFinite=false; bool anyWeight=false;
                    for (size_t li=0; li<layers_.size(); ++li) {
                        const auto& L = layers_[li];
                        const float* hSrc = heightsStorage_.data() + L.offset;
                        float v = hSrc[i];
                        if (!std::isfinite(v)) continue;
                        anyFinite = true;
                        float c=1.0f;
                        if (L.hasConfidence) {
                            const float* cSrc = confidenceStorage_.data() + L.confOffset;
                            float rawC = cSrc[i]; if(!(rawC>=0.f) || !std::isfinite(rawC)) rawC=0.f; if(rawC>1.f) rawC=1.f; c=rawC;
                        }
                        if (c>0.f) { anyWeight=true; sumR += c; sumRH += double(c) * double(v); }
                    }
                    if (!anyFinite) { outHeightMap[i]=0.0f; ++fbEmpty; continue; }
                    if (!anyWeight || sumR <= 0.0) {
                        // fallback min-z
                        float best = std::numeric_limits<float>::infinity();
                        for (const auto& L : layers_) {
                            const float* hSrc = heightsStorage_.data() + L.offset; float v = hSrc[i]; if(!std::isfinite(v)) continue; if(v<best) best=v; }
                        if (best == std::numeric_limits<float>::infinity()) { outHeightMap[i]=0.0f; ++fbEmpty; }
                        else { outHeightMap[i]=best; ++fusedValid; ++fbMinZ; }
                    } else {
                        float hv = static_cast<float>(sumRH / sumR);
                        outHeightMap[i] = hv; ++fusedValid;
                    }
                }
                if (outConfidence) {
                    for (size_t i=0;i<total;++i) {
                        // Weighted composite confidence: reuse pass to avoid recompute (simple re-loop acceptable N small)
                        double sumR=0.0, sumRC=0.0; bool anyFinite=false; for (const auto& L: layers_) {
                            const float* hSrc = heightsStorage_.data() + L.offset; float v=hSrc[i]; if(!std::isfinite(v)) continue; anyFinite=true; float c=1.0f; if(L.hasConfidence){ const float* cSrc=confidenceStorage_.data()+L.confOffset; float rawC=cSrc[i]; if(!(rawC>=0.f) || !std::isfinite(rawC)) rawC=0.f; if(rawC>1.f) rawC=1.f; c=rawC; } if(c>0.f){ sumR+=c; sumRC+=c*c; } }
                        if(!anyFinite){ (*outConfidence)[i]=0.0f; continue; }
                        if(sumR<=0.0){ (*outConfidence)[i]=0.0f; continue; }
                        (*outConfidence)[i]= static_cast<float>(sumRC / sumR); // weighted average of confidences
                    }
                }
            } else {
                // Min-z path (original)
                for (size_t i=0;i<total;++i) {
                    float best = std::numeric_limits<float>::infinity();
                    for (const auto& L : layers_) {
                        const float* hSrc = heightsStorage_.data() + L.offset;
                        float v = hSrc[i]; if (!std::isfinite(v)) continue; if (v < best) best = v;
                    }
                    if (best == std::numeric_limits<float>::infinity()) { outHeightMap[i]=0.0f; ++fbEmpty; }
                    else { outHeightMap[i]=best; ++fusedValid; }
                }
                if (outConfidence) {
                    for (size_t i=0;i<total;++i) {
                        float cBest = 0.0f;
                        for (const auto& L : layers_) {
                            if (!L.hasConfidence) { cBest = std::max(cBest, 1.0f); continue; }
                            const float* cSrc = confidenceStorage_.data() + L.confOffset;
                            float cv = cSrc[i]; if (std::isfinite(cv)) cBest = std::max(cBest, cv);
                        }
                        (*outConfidence)[i] = cBest;
                    }
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
