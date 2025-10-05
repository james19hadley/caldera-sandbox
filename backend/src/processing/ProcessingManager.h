#ifndef CALDERA_BACKEND_PROCESSING_MANAGER_H
#define CALDERA_BACKEND_PROCESSING_MANAGER_H

#include <functional>
#include <memory>
#include <vector>

#include "common/DataTypes.h"
#include "processing/IHeightMapFilter.h"
#include "processing/ProcessingTypes.h"
#include "processing/FusionAccumulator.h"

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

    struct StabilityMetrics {
        uint64_t frameId = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t hardInvalid = 0;        // geometric invalidations
        float stabilityRatio = 0.0f;      // (stableCount / validConsidered)
        float avgVariance = 0.0f;         // running EMA of per-pixel variance proxy
        float procTotalMs = 0.0f;         // end-to-end processing time (approx)
        float buildMs = 0.0f;             // build+validate time
        float filterMs = 0.0f;            // temporal+spatial filter time
        float fuseMs = 0.0f;              // fusion time
        // Adaptive (Phase 2) metrics
        float adaptiveSpatial = 0.0f;     // 1 if spatial filter active via adaptive logic
        float adaptiveStrong = 0.0f;      // 1 if strong (double-pass) applied
        uint32_t adaptiveStreak = 0;      // current unstable streak while active
        // Spatial effectiveness sampling (ratio <1 means variance reduced). 0 if not computed.
        float spatialVarianceRatio = 0.0f; // postSpatialVariance / preSpatialVariance (sampled)
        float adaptiveTemporalBlend = 0.0f; // 1 if adaptive temporal blending applied this frame
    };

    const StabilityMetrics& lastStabilityMetrics() const { return lastStabilityMetrics_; }

    const FrameValidationSummary& lastValidationSummary() const { return lastValidationSummary_; }

    // Allow tests / higher layers to inject transform & plane parameters (per-sensor for now)
    void setTransformParameters(const TransformParameters& p) {
        transformParams_ = p;
        transformParamsReady_ = true;
    }

    // Convenience: load plane bounds + depth scale from a calibration profile (minimal subset)
    template <typename CalibrationProfileLike>
    void applyCalibrationProfile(const CalibrationProfileLike& profile) {
        // Expect fields: basePlaneCalibration.basePlane (a,b,c,d), minValidPlane, maxValidPlane, and optional depth scale
        transformParams_.planeA = profile.basePlaneCalibration.basePlane.a;
        transformParams_.planeB = profile.basePlaneCalibration.basePlane.b;
        transformParams_.planeC = profile.basePlaneCalibration.basePlane.c;
        transformParams_.planeD = profile.basePlaneCalibration.basePlane.d;
        transformParams_.minValidPlane = {profile.minValidPlane.a, profile.minValidPlane.b, profile.minValidPlane.c, profile.minValidPlane.d};
        transformParams_.maxValidPlane = {profile.maxValidPlane.a, profile.maxValidPlane.b, profile.maxValidPlane.c, profile.maxValidPlane.d};
        transformParamsReady_ = true;
        planeOffsetsApplied_ = false; // allow env offsets to apply once with new params
        // Depth scale: if profile has intrinsic calibration with depth correction (future), we could override scale_ here.
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
    FusionAccumulator fusion_; // Phase 0 scaffold (single-sensor passthrough)
    // Stability instrumentation
    bool metricsEnabled_ = false;
    StabilityMetrics lastStabilityMetrics_{};
    // Simple running exponential moving average for variance proxy
    float emaVariance_ = 0.0f;
    // Adaptive control
    int adaptiveMode_ = 0; // 0=off,1=static behavior,2=adaptive spatial toggle
    float adaptiveStabilityMin_ = 0.85f; // below this -> enable spatial
    float adaptiveVarianceMax_ = 0.02f;  // above this -> enable spatial
    // Phase 2 hysteresis & strong mode
    bool adaptiveSpatialActive_ = false;
    uint32_t unstableStreak_ = 0;
    uint32_t stableStreak_ = 0;
    int adaptiveOnStreak_ = 2;   // need N consecutive unstable frames (based on prev metrics) to enable
    int adaptiveOffStreak_ = 3;  // need N consecutive stable frames to disable
    float adaptiveStrongVarMult_ = 2.0f;       // variance multiplier trigger
    float adaptiveStrongStabFrac_ = 0.75f;     // stability fraction trigger
    bool adaptiveStrongDoublePass_ = true;     // double-pass strong filtering
    // Adaptive temporal scaling
    float adaptiveTemporalScale_ = 1.0f; // >1 enables extra temporal smoothing when unstable
    std::vector<float> prevFilteredHeight_; // buffer after spatial (pre-fusion) from previous frame
    bool prevFilteredValid_ = false;
};

} // namespace caldera::backend::processing

#endif // CALDERA_BACKEND_PROCESSING_MANAGER_H
