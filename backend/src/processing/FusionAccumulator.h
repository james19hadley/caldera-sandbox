#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <limits>

namespace caldera::backend::processing {

struct FusionInputLayer {
    std::string sensorId;            // logical sensor id
    const float* heights = nullptr;  // height map pointer (size = width*height)
    const float* confidence = nullptr; // optional (nullable until confidence map implemented)
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
    void beginFrame(uint64_t frameId, int width, int height) {
        frameId_ = frameId;
        width_ = width;
        height_ = height;
        layers_.clear();
    }

    void addLayer(const FusionInputLayer& layer) {
        if (layer.heights && layer.width == width_ && layer.height == height_) {
            layers_.push_back(layer);
        }
        // else ignore (mismatch); future: collect stats / warnings
    }

    // Fuse into outHeightMap; ensures size = width_*height_ (resizes if needed)
    // outConfidence optional (ignored in Phase 0)
    void fuse(std::vector<float>& outHeightMap, std::vector<float>* outConfidence = nullptr) const {
        const size_t total = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        if (outHeightMap.size() != total) outHeightMap.resize(total, 0.0f);
        if (outConfidence && outConfidence->size() != total) outConfidence->resize(total, 1.0f);

        if (layers_.empty()) {
            // produce zero baseline
            std::fill(outHeightMap.begin(), outHeightMap.end(), 0.0f);
            if (outConfidence) std::fill(outConfidence->begin(), outConfidence->end(), 0.0f);
            return;
        }
        if (layers_.size() == 1) {
            const FusionInputLayer& L = layers_[0];
            std::copy(L.heights, L.heights + total, outHeightMap.data());
            if (outConfidence) {
                if (L.confidence) std::copy(L.confidence, L.confidence + total, outConfidence->data());
                else std::fill(outConfidence->begin(), outConfidence->end(), 1.0f);
            }
            return;
        }
        // Placeholder multi-layer strategy (Phase 1 TODO): min-z of valid (> -inf) values
        std::fill(outHeightMap.begin(), outHeightMap.end(), 0.0f);
        for (size_t idx = 0; idx < total; ++idx) {
            float chosen = std::numeric_limits<float>::infinity();
            for (const auto& L : layers_) {
                float v = L.heights[idx];
                // Treat zero baseline vs actual zero height the same for now; future: need NaN semantics
                if (v < chosen) chosen = v;
            }
            outHeightMap[idx] = (chosen == std::numeric_limits<float>::infinity()) ? 0.0f : chosen;
        }
        if (outConfidence) std::fill(outConfidence->begin(), outConfidence->end(), 1.0f); // placeholder
    }

    size_t layerCount() const { return layers_.size(); }
    uint64_t frameId() const { return frameId_; }

private:
    uint64_t frameId_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<FusionInputLayer> layers_;
};

} // namespace caldera::backend::processing
