// FusionStage.h
// Encapsulates fusion accumulator invocation. Currently delegates to ProcessingManager's
// existing fusion logic; placeholder apply becomes active in stage-driven refactor (Todo 6).

#pragma once
#include "processing/ProcessingStages.h"
#include <functional>

namespace caldera::backend::processing {

class FusionStage : public IProcessingStage {
public:
    // Accept a functor which performs fusion when called with FrameContext.
    using FusionFn = std::function<void(FrameContext&)>;
    explicit FusionStage(FusionFn fn): fn_(std::move(fn)) {}
    const char* name() const override { return "fusion"; }
    void apply(FrameContext& ctx) override { if(fn_) fn_(ctx); }
private:
    FusionFn fn_;
};

} // namespace caldera::backend::processing
