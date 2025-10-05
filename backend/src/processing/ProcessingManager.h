#ifndef CALDERA_BACKEND_PROCESSING_MANAGER_H
#define CALDERA_BACKEND_PROCESSING_MANAGER_H

#include <functional>
#include <memory>
#include <vector>
#include <mutex>

#include "common/DataTypes.h"
#include "processing/IHeightMapFilter.h"
#include "processing/ProcessingTypes.h"
#include "processing/FusionAccumulator.h"
#include "processing/ProcessingStages.h" // stage scaffolding (future use)
#include "processing/PipelineParser.h" // StageSpec definition

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
        // Edge preservation sampling (ratio close to 1 means edges preserved). 0 if not computed.
        float spatialEdgePreservationRatio = 0.0f; // postEdgeEnergy / preEdgeEnergy (sampled gradients)
        // Confidence (M5) aggregates (only valid if confidence map enabled this frame)
        float meanConfidence = 0.0f;
        float fractionLowConfidence = 0.0f;
        float fractionHighConfidence = 0.0f;
    };

    // Provide an alias struct so stage scaffolding (which cannot include this header to avoid cycles)
    // can refer to an opaque-compatible metrics object. Layout compatibility is not yet required
    // because stages are not instantiated; this acts as a forward anchor for future refactor.
    struct ProcessingManagerStabilityMetricsOpaque : public StabilityMetrics {};

    const StabilityMetrics& lastStabilityMetrics() const { return lastStabilityMetrics_; }

    // Confidence map accessor: returns view of last computed map (empty if disabled or size mismatch)
    const std::vector<float>& confidenceMap() const { return confidenceMap_; }

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
    // Helpers (refactor targets) used by both legacy and pipeline execution paths
    void applyTemporalFilter(std::vector<float>& heightMap, int w, int h);
    struct SpatialApplyResult {
        bool applied=false;
        bool strong=false;
        bool sampled=false;
        float preVar=0.f;
        float postVar=0.f;
        float preEdge=0.f;
        float postEdge=0.f;
    };
    SpatialApplyResult applySpatialFilter(std::vector<float>& heightMap,
                                          int w,
                                          int h,
                                          const std::string& altKernel,
                                          bool applySpatial,
                                          bool strongPass,
                                          bool metricsEnabled,
                                          int sampleCount);

    // Shared metrics/confidence aggregation (used by both legacy and stage execution paths)
    void updateMetrics(const std::vector<float>& fusedHeights,
                       uint32_t width,
                       uint32_t height,
                       const std::chrono::steady_clock::time_point& tBuildStart,
                       const std::chrono::steady_clock::time_point& tBuildEnd,
                       const std::chrono::steady_clock::time_point& tFuseStart,
                       const std::chrono::steady_clock::time_point& tFuseEnd,
                       const std::chrono::steady_clock::time_point& tFrameEnd,
                       const SpatialApplyResult& spatialRes,
                       bool adaptiveTemporalApplied);

    // Attempt to parse CALDERA_PIPELINE once (lazy executed in ctor). Purely informational until
    // stage execution refactor lands. If parsing fails we keep specs_ empty and mark fallback.
    void parsePipelineEnv();

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
    // Confidence map support (M5 MVP)
    bool confidenceEnabled_ = false; // env CALDERA_ENABLE_CONFIDENCE_MAP
    std::vector<float> confidenceMap_; // same dimensions as height map when enabled
    bool exportConfidence_ = false; // CALDERA_PROCESSING_EXPORT_CONFIDENCE
    float confWeightS_ = 0.6f; // wS
    float confWeightR_ = 0.25f; // wR
    float confWeightT_ = 0.15f; // wT
    float confLowThresh_ = 0.3f;
    float confHighThresh_ = 0.8f;
    // Stage-oriented architecture (M5 scaffolding): not yet driving execution.
    // Will be populated with concrete stage objects (build, plane_validate, temporal, etc.)
    // in subsequent steps without breaking existing processRawDepthFrame logic.
    std::vector<std::unique_ptr<IProcessingStage>> stages_; // populated from parsed pipeline specs (instantiated, not yet executed)
    void rebuildPipelineStages(); // instantiate stage objects based on parsed specs
    AdaptiveState adaptiveState_; // shared state for future adaptive_control + spatial stages
    // Parsed pipeline specs (M5 Step 2). Not yet executed; retained for diagnostics/testing.
    std::vector<StageSpec> parsedPipelineSpecs_;
    bool pipelineSpecValid_ = false;
    std::string pipelineSpecError_;
    // Experimental multi-layer fusion duplication (development/testing): if enabled creates a second synthetic layer
    bool duplicateFusionLayer_ = false; // CALDERA_FUSION_DUP_LAYER=1
    float duplicateFusionShift_ = 0.02f; // CALDERA_FUSION_DUP_LAYER_SHIFT
    float duplicateFusionBaseConf_ = 0.9f; // CALDERA_FUSION_DUP_LAYER_CONF (base,dup)
    float duplicateFusionDupConf_ = 0.5f;
    bool profileLoaded_ = false; // true when a calibration profile successfully loaded (skip env plane overrides)
    // Thread-safety: Phase 0 design assumed single-sensor feed. Multi-sensor tests invoke
    // processRawDepthFrame concurrently from multiple SyntheticSensorDevice threads, which led
    // to data races (and a heap-use-after-free via FusionAccumulator using a pointer to a
    // stack-local vector from another thread). For now we serialize the entire processing
    // pipeline per frame with a coarse mutex. Future Phase (pipeline orchestration) can
    // introduce finer-grained stage isolation or per-sensor instances.
    // Persistent reusable buffers (memory stability)
    std::vector<float> heightMapBuffer_;
    std::vector<uint8_t> validityBuffer_;
    std::vector<float> layerHeightsBuffer_;
    std::vector<float> layerConfidenceBuffer_;
    std::vector<float> fusedHeightsBuffer_;
    std::vector<float> fusedConfidenceBuffer_;
    InternalPointCloud reusableCloudIn_;
    InternalPointCloud reusableCloudFiltered_;
    // Track original invalid pixels (pre zero-fill) for counting & confidence zeroing
    std::vector<uint8_t> originalInvalidMask_;
    mutable std::mutex processMutex_;
};

} // namespace caldera::backend::processing

#endif // CALDERA_BACKEND_PROCESSING_MANAGER_H
