// IHeightMapFilter.h
// Optional processing hook for height map stabilization / filtering.
// Current implementation provides only a NoOp filter; future filters (temporal smoothing,
// variance-based denoise, hysteresis) can implement this interface and be injected into
// ProcessingManager without changing its external contract.

#ifndef CALDERA_BACKEND_PROCESSING_IHEIGHTMAPFILTER_H
#define CALDERA_BACKEND_PROCESSING_IHEIGHTMAPFILTER_H

#include <vector>

namespace caldera::backend::processing {

class IHeightMapFilter {
public:
    virtual ~IHeightMapFilter() = default;
    // Mutates 'data' in-place. Width/height supplied for kernels needing topology awareness.
    virtual void apply(std::vector<float>& data, int width, int height) = 0;
};

class NoOpHeightMapFilter final : public IHeightMapFilter {
public:
    void apply(std::vector<float>&, int, int) override { /* intentional no-op */ }
};

} // namespace caldera::backend::processing

#endif // CALDERA_BACKEND_PROCESSING_IHEIGHTMAPFILTER_H
