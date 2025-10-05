#pragma once
#include "processing/ProcessingStages.h"
#include "processing/IHeightMapFilter.h"
#include <memory>

namespace caldera::backend::processing {

class TemporalStage : public IProcessingStage {
public:
    explicit TemporalStage(std::shared_ptr<IHeightMapFilter> filter): filter_(std::move(filter)) {}
    const char* name() const override { return "temporal"; }
    void apply(FrameContext& ctx) override {
        if (!filter_) return;
        filter_->apply(ctx.height, static_cast<int>(ctx.width), static_cast<int>(ctx.heightPx));
    }
private:
    std::shared_ptr<IHeightMapFilter> filter_;
};

} // namespace caldera::backend::processing
