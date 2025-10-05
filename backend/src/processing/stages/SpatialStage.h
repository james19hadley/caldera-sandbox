#pragma once
#include "processing/ProcessingStages.h"
#include <string>

namespace caldera::backend::processing {

// Forward minimal spatial application wrapper (will delegate to ProcessingManager's helper for now)
class SpatialStage : public IProcessingStage {
public:
    SpatialStage(std::string altKernel): altKernel_(std::move(altKernel)) {}
    const char* name() const override { return "spatial"; }
    void apply(FrameContext& ctx) override {
        // Placeholder: real spatial logic still in ProcessingManager::applySpatialFilter.
        // This stage will become active after migration of that logic; currently no-op.
        (void)ctx; // suppress unused
    }
private:
    std::string altKernel_;
};

} // namespace caldera::backend::processing
