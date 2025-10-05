// BuildStage.h
// Stage placeholder for point cloud build + validation. Currently a no-op wrapper
// because build/validate still occurs inline in ProcessingManager prior to stage loop.
// Will be activated during migration of processRawDepthFrame to stage-driven execution (Todo 6).

#pragma once
#include "processing/ProcessingStages.h"

namespace caldera::backend::processing {

class BuildStage : public IProcessingStage {
public:
    BuildStage() = default;
    const char* name() const override { return "build"; }
    void apply(FrameContext& ctx) override {
        (void)ctx; // placeholder (build already executed earlier in current architecture)
    }
};

} // namespace caldera::backend::processing
