#ifndef CALDERA_BACKEND_PROCESSING_MANAGER_H
#define CALDERA_BACKEND_PROCESSING_MANAGER_H

#include <functional>
#include <memory>
#include <vector>

#include "common/DataTypes.h"
#include "processing/IHeightMapFilter.h"
#include "processing/ProcessingTypes.h"

namespace spdlog { class logger; }

namespace caldera::backend::processing {

class ProcessingManager {
public:
    using WorldFrame = caldera::backend::common::WorldFrame;
    using RawDepthFrame = caldera::backend::common::RawDepthFrame;
    using WorldFrameCallback = std::function<void(const WorldFrame&)>;

    ProcessingManager(std::shared_ptr<spdlog::logger> orchestratorLogger,
              std::shared_ptr<spdlog::logger> fusionLogger = nullptr,
              float depthToHeightScale = -1.0f); // if <0 auto-resolve (env or default 0.001)

    void setWorldFrameCallback(WorldFrameCallback cb);

    void processRawDepthFrame(const RawDepthFrame& raw);

    // Inject a height map filter (ownership shared to allow reuse in tests). If not set, no-op.
    void setHeightMapFilter(std::shared_ptr<IHeightMapFilter> f) { height_filter_ = std::move(f); }

    struct FrameValidationSummary {
        uint32_t valid = 0;
        uint32_t invalid = 0;
    };

    const FrameValidationSummary& lastValidationSummary() const { return lastValidationSummary_; }

    // Allow tests / higher layers to inject transform & plane parameters (per-sensor for now)
    void setTransformParameters(const TransformParameters& p) {
        transformParams_ = p;
        transformParamsReady_ = true;
    }

private:
    // Build an internal point cloud (minimal world-space) & validate points.
    void buildAndValidatePointCloud(const RawDepthFrame& raw,
                                    InternalPointCloud& cloud,
                                    FrameValidationSummary& summary);

private:
    std::shared_ptr<spdlog::logger> orch_logger_;
    std::shared_ptr<spdlog::logger> fusion_logger_;
    WorldFrameCallback callback_;
    uint64_t frameCounter_ = 0;
    float scale_ = 0.001f;
    std::shared_ptr<IHeightMapFilter> height_filter_{}; // optional
    FrameValidationSummary lastValidationSummary_{};
    TransformParameters transformParams_{};
    bool transformParamsReady_ = false;
    bool planeOffsetsApplied_ = false; // guard to only apply env overrides once
};

} // namespace caldera::backend::processing

#endif // CALDERA_BACKEND_PROCESSING_MANAGER_H
