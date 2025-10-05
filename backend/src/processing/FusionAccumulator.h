#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <limits>

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
    void beginFrame(uint64_t frameId, int width, int height) {
        frameId_ = frameId;
        width_ = width;
        height_ = height;
        layers_.clear();
        // Reuse internal storage buffers sized for current frame
        size_t total = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        if (heightsStorage_.size() != total) heightsStorage_.assign(total, 0.0f);
        if (confidenceStorage_.size() != total) confidenceStorage_.assign(total, 1.0f);
    }

    void addLayer(const FusionInputLayer& layer) {
        if (layer.heights && layer.width == width_ && layer.height == height_) {
            // Copy heights (and confidence if provided) into owned contiguous storage.
            size_t total = static_cast<size_t>(width_) * static_cast<size_t>(height_);
            // For now only support one layer copy; multi-layer Phase 1 will segment storage.
            // Store offsets for each layer.
            LayerEntry entry;
            entry.sensorId = layer.sensorId;
            entry.offset = 0; // single layer
            if (heightsStorage_.size() < total) heightsStorage_.resize(total);
            std::copy(layer.heights, layer.heights + total, heightsStorage_.data());
            if (layer.confidence) {
                if (confidenceStorage_.size() < total) confidenceStorage_.resize(total);
                std::copy(layer.confidence, layer.confidence + total, confidenceStorage_.data());
                entry.hasConfidence = true;
            }
            layers_.push_back(entry);
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
            const LayerEntry& L = layers_[0];
            const float* hSrc = heightsStorage_.data() + L.offset;
            std::copy(hSrc, hSrc + total, outHeightMap.data());
            if (outConfidence) {
                if (L.hasConfidence) {
                    const float* cSrc = confidenceStorage_.data() + L.offset;
                    std::copy(cSrc, cSrc + total, outConfidence->data());
                } else {
                    std::fill(outConfidence->begin(), outConfidence->end(), 1.0f);
                }
            }
            return;
        }
        // Placeholder multi-layer strategy (Phase 1 TODO): min-z of valid (> -inf) values
        std::fill(outHeightMap.begin(), outHeightMap.end(), 0.0f);
        for (size_t idx = 0; idx < total; ++idx) {
            float chosen = std::numeric_limits<float>::infinity();
            for (const auto& L : layers_) {
                const float* hSrc = heightsStorage_.data() + L.offset;
                float v = hSrc[idx];
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
    struct LayerEntry {
        std::string sensorId;
        size_t offset = 0; // offset into storage buffers
        bool hasConfidence = false;
    };
    std::vector<LayerEntry> layers_;
    std::vector<float> heightsStorage_;
    std::vector<float> confidenceStorage_;
};

} // namespace caldera::backend::processing
