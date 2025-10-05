#pragma once
#include "processing/ProcessingStages.h"
#include <functional>
#include <string>

namespace caldera::backend::processing {

class LambdaStage : public IProcessingStage {
public:
    using Fn = std::function<void(FrameContext&)>;
    LambdaStage(std::string name, Fn fn) : name_(std::move(name)), fn_(std::move(fn)) {}
    const char* name() const override { return name_.c_str(); }
    void apply(FrameContext& ctx) override { if(fn_) fn_(ctx); }
private:
    std::string name_;
    Fn fn_;
};

} // namespace caldera::backend::processing
