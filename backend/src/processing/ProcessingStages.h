// ProcessingStages.h
// Stage-oriented processing architecture scaffolding (M5 Step 1)
// Provides minimal interfaces (FrameContext, AdaptiveState, IProcessingStage)
// without altering existing ProcessingManager execution flow yet.

#pragma once

#include <cstdint>
#include <vector>

namespace caldera::backend::processing {

struct AdaptiveState {
    bool spatialActive = false;     // gating for baseline spatial smoothing
    bool strongActive = false;      // gating for strong spatial stage
    uint32_t unstableStreak = 0;    // consecutive unstable frames
    uint32_t stableStreak = 0;      // consecutive stable frames
    float lastStability = 0.f;      // previous frame stabilityRatio
    float lastVariance = 0.f;       // previous frame avgVariance proxy
        std::string strongKernelChoice = "classic_double"; // parsed from CALDERA_ADAPTIVE_STRONG_KERNEL
    float temporalBlendApplied = 0.f; // 1.0f if adaptive temporal blend applied this frame
};

// Forward declarations to avoid heavy includes.
struct TransformParameters; // defined in ProcessingTypes.h
class ProcessingManager;     // existing manager

// Forward for metrics struct (lives inside ProcessingManager currently).
// We reference it by fully qualified name in FrameContext to avoid duplication.

// Forward declare metrics struct (defined inside ProcessingManager currently); we will
// redefine a minimal alias when integrating full stage execution. For now we only need
// an opaque reference holder to avoid circular include.
struct ProcessingManagerStabilityMetricsOpaque;

struct FrameContext {
    std::vector<float>& height;                 // world-space height buffer (mutable)
    std::vector<uint8_t>& validityMask;         // 1=valid, 0=invalid
    std::vector<float>* confidence;             // optional confidence map (nullptr if disabled)
    ProcessingManagerStabilityMetricsOpaque& metrics; // opaque metrics reference
    AdaptiveState& adaptive;                    // adaptive gating & historical state
    const TransformParameters& transform;       // calibration / plane parameters
    uint32_t width;                             // width in pixels
    uint32_t heightPx;                          // height in pixels
    uint64_t frameId;                           // sequential frame id
};

class IProcessingStage {
public:
    virtual ~IProcessingStage() = default;
    virtual const char* name() const = 0;
    virtual void apply(FrameContext& ctx) = 0; // may mutate height/confidence/metrics/adaptive
};

} // namespace caldera::backend::processing
