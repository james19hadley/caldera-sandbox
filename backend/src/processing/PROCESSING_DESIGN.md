# Processing Layer Design (Stage-Oriented Architecture)

Last Updated: 2025-10-05
Status: Working design capturing current CPU implementation + near-term (M5) evolution.

## 1. Rationale & Direction
Legacy (SARndbox) pipeline is implicitly ordered, hard-wired, and always-on for certain filters (spatial). We need:
* Declarative ordering (like listing layers in a neural network model definition).
* Hot-path efficiency (O(N) pixel loops dominate; orchestration overhead must be negligible).
* Extensibility (insert experimental stages: alternative kernels, diagnostic exports, confidence map) with minimal code churn.
* Adaptive logic separated from transform kernels.

GPU ambitions remain long-term; current focus is a robust CPU stage graph that can map 1:1 to future GPU node kernels.

## 2. Stage Model
### 2.1 Interfaces
```
struct FrameContext {
    std::vector<float>& height;          // height/elevation buffer in-place
    std::vector<uint8_t>& validityMask;  // 1=valid, 0=invalid (hard invalid)
    std::vector<float>* confidence;      // optional confidence map (nullptr if disabled)
    ProcessingManager::StabilityMetrics& metrics; // frame-level metrics aggregate
    AdaptiveState& adaptive;             // gating flags + historical stability
    const TransformParameters& transform;// plane / scale params
    uint32_t width;
    uint32_t heightPx;
    uint64_t frameId;
};

struct AdaptiveState {
    bool spatialActive=false;
    bool strongActive=false;
    uint32_t unstableStreak=0;
    uint32_t stableStreak=0;
    float lastStability=0.f;
    float lastVariance=0.f;
    float temporalBlendApplied=0.f; // 1.0 if extra temporal blend applied this frame
};

class IProcessingStage {
public:
    virtual ~IProcessingStage() = default;
    virtual const char* name() const = 0;
    virtual void apply(FrameContext& ctx) = 0; // may mutate height/confidence/metrics/adaptive
};
```

### 2.2 Stage Categories
| Category | Example | Role |
|----------|---------|------|
| Geometry | build, plane_validate | Raw depth → world height + plane clipping |
| Temporal | temporal | EMA/hysteresis smoothing |
| Control  | adaptive_control | Updates AdaptiveState flags (no pixel mutate) |
| Spatial  | spatial(classic), spatial(fastgauss) | Conditional smoothing based on flags |
| Derived  | confidence | Per-pixel confidence & aggregates |
| Fusion   | fuse | Multi-sensor combination (Phase 0 passthrough) |
| Diagnostics | diagnostics | Future: export sampling / ring buffer |

### 2.3 Conditional Execution (`when`)
Each stage declares a condition: `always | adaptive | adaptiveStrong | never`.
AdaptiveControlStage sets `adaptive.spatialActive` and `adaptive.strongActive` based on previous frame metrics + hysteresis thresholds.

## 3. Pipeline Specification Grammar
Environment variable: `CALDERA_PIPELINE`
```
PIPELINE := STAGE ("," STAGE)*
STAGE := IDENTIFIER [ "(" PARAM_LIST ")" ]
PARAM_LIST := PARAM ("," PARAM)*
PARAM := KEY "=" VALUE
```
Example:
```
CALDERA_PIPELINE=build,plane_validate,temporal,adaptive_control,\
    spatial(mode=classic,when=adaptive),\
    spatial(mode=fastgauss,sigma=1.6,when=adaptiveStrong),confidence,fuse
```
If absent → default pipeline (Adaptive Balanced) is used.

### 3.1 Spatial Stage Parameters
* mode=`classic|wide5|fastgauss`
* passes=`N` (classic/wide5 only; replicates double-pass strong mode)
* sigma=`float` (fastgauss only)
* when=`always|adaptive|adaptiveStrong|never`

### 3.2 Confidence Stage Parameters
* weights=`wS,wR,wT` (override env)  
* low=`float` low threshold (default 0.3)  
* high=`float` high threshold (default 0.8)  
* when=always (but internally no-op if `CALDERA_ENABLE_CONFIDENCE_MAP=0`)

## 4. Default Pipelines
### 4.1 Adaptive Balanced (current default)
```
build,plane_validate,temporal,adaptive_control,\
spatial(mode=classic,when=adaptive),\
spatial(mode=classic,passes=2,when=adaptiveStrong),confidence,fuse
```
### 4.2 Legacy Emulation
```
build,plane_validate,temporal,spatial(mode=classic,when=always),fuse
```
### 4.3 High Smooth Variant
```
build,plane_validate,temporal,adaptive_control,\
spatial(mode=classic,when=adaptive),\
spatial(mode=fastgauss,sigma=1.8,when=adaptiveStrong),confidence
```

## 5. Confidence Map (M5 Phase A)
Inputs (frame scope MVP): Validity V, stabilityRatio S, spatialVarianceRatio R (fallback 1), adaptiveTemporalBlend T.
Weights: wS=0.6, wR=0.25, wT=0.15 (env `CALDERA_CONFIDENCE_WEIGHTS` or stage param). Absent component → drop weight + renormalize.
Formula:
```
if !V -> 0
c = (wS*S + wR*(1 - clamp(R,0,1)) + wT*T)/(wS+wR+wT)
```
Clamp to [0,1].
Aggregates stored in metrics: meanConfidence, fractionLow (<low), fractionHigh (>high).
Env gating: `CALDERA_ENABLE_CONFIDENCE_MAP=1`.
Future: per-pixel temporal variance to replace global S; edge-aware adjustment.

## 6. Fast Gaussian Integration
SpatialStage chooses kernel via enum (Kernel::Classic3, ::Wide5, ::FastGaussian). For classic/wide5, multi-pass imitates stronger smoothing. For fastgauss, a single pass with sigma parameter should approximate double-pass classic (sigma tune ~1.4–1.6).
Metrics sampling (variance ratio) executed once per logical spatial smoothing to avoid bias from multi-pass.
Future optional metric: gradientEnergyBefore/After for edge preservation.

## 7. Adaptive Control Stage
Reads previous frame `stabilityRatio` / `avgVariance`, applies hysteresis streak thresholds (`CALDERA_ADAPTIVE_ON_STREAK`, `CALDERA_ADAPTIVE_OFF_STREAK`), sets `spatialActive` and optionally `strongActive`. Also sets `temporalBlendApplied` if instability triggers adaptive temporal scaling (`CALDERA_ADAPTIVE_TEMPORAL_SCALE`).

## 8. Metrics Summary
Existing: stabilityRatio, avgVariance, spatialVarianceRatio, adaptiveSpatial, adaptiveStrong, adaptiveStreak, adaptiveTemporalBlend.
Added (M5): meanConfidence, fractionLowConfidence, fractionHighConfidence (only when enabled).
Sampling size: `CALDERA_SPATIAL_SAMPLE_COUNT` reused.

## 9. Performance Notes
* One virtual call per stage (~negligible vs pixel loops).
* All scratch buffers reused (no per-frame allocations when steady-state reached).
* Confidence map adds one linear pass when enabled.

## 10. Implementation Order (Detailed)
1. Stage interfaces + FrameContext/AdaptiveState definitions (no behavior change yet).
2. Refactor ProcessingManager: wrap current logic into BuildStage, PlaneValidateStage, TemporalStage, AdaptiveControlStage, SpatialStage (classic baseline), SpatialStageStrong (classic double-pass) – preserve existing outputs.
3. Implement ConfidenceStage MVP (+ env flags, metrics aggregates, tests).
4. Pipeline string parser (CALDERA_PIPELINE) constructing stage list; error-handling with fallback to default ordering.
5. Integrate FastGaussian kernel into SpatialStage (mode=fastgauss, sigma env/param) + tests vs variance ratio.
6. Replace double-pass strong with dedicated second spatial stage; retire internal double-pass branch.
7. Optional gradient energy sampling metric (diagnostic logging only) to compare strong strategies.
8. Add `CALDERA_ADAPTIVE_STRONG_KERNEL` allowing selection: classic|wide5|fastgauss (resolves which strong spatial stage is created).
9. Documentation & design doc update (this file) + update implementation plan changelog.
10. (Future) Per-pixel temporal variance stage stub (disabled by default) to upgrade confidence formula.

## 11. Testing Matrix Additions
| Test | Purpose |
|------|---------|
| ConfidenceInvalidZero | Invalid → 0 confidence |
| ConfidenceStabilityInfluence | Mean increases with S |
| ConfidenceSpatialBenefit | Spatial improvement raises mean |
| ConfidenceTemporalInfluence | Temporal blend raises mean within cap |
| PipelineOrderTest | Honors CALDERA_PIPELINE sequence |
| FastGaussianVarRatioTest | Ensure fastgauss variance reduction ≤ double-pass classic + ε |
| StrongKernelSelectionTest | Proper kernel chosen via CALDERA_ADAPTIVE_STRONG_KERNEL |
| LegacyEmulationTest | Legacy pipeline matches previous outputs |

## 12. Backward Compatibility
No pipeline env → identical to pre-stage-refactor behavior (adaptive + optional confidence disabled). Setting `CALDERA_PIPELINE` overrides order. Confidence stage inert unless explicitly enabled.

## 13. Future Extensions
* JSON pipeline configuration.
* Fusion strategies (min-z, confidence-weighted, time-consensus).
* Per-pixel defect map integration into confidence.
* Diagnostics ring buffer / UI introspection API.

## 14. Open Questions
1. Runtime (hot) pipeline reconfiguration? Initial scope: startup only.
2. Automatic kernel selection using gradient metric? (Defer until data gathered.)
3. Confidence external publishing channel now or after fusion weighting? (Likely after.)

## 15. Legacy Vision Appendix (Retained)
High-level GPU-first multi-sensor, motion-aware, and advanced filtering aspirations from earlier draft remain valid. They are intentionally pruned from the active near-term plan above to keep M5 scope lean. See previous revision sections for aspirational features (anisotropic diffusion, multi-resolution pyramids, ML noise models) to be revisited post M6.

---
End of current stage-oriented processing design.

## Component Design Specifications

### 1. Preprocessing Stage

#### 1.1 Depth Correction Module
**Purpose:** Compensate for optical distortions and sensor-specific artifacts

**Components:**
- `DepthCorrector` class with per-sensor correction profiles
- `CalibrationManager` for loading/storing correction coefficients
- `DistortionModel` interface supporting different correction algorithms

**Implementation:**
```cpp
class DepthCorrector {
    struct CorrectionProfile {
        std::vector<float> pixelCorrections;
        Matrix3f intrinsicMatrix;
        Vector4f distortionCoeffs;
    };
    
    CorrectionProfile getProfile(const std::string& sensorId);
    void correctDepthFrame(const RawFrame& input, CorrectedFrame& output);
};
```

#### 1.2 Coordinate Transform Module
**Purpose:** Convert from sensor space to world coordinate system

**Transformations:**
1. **Sensor → Camera Space** (intrinsic calibration)
2. **Camera → World Space** (extrinsic calibration)
3. **Multi-sensor registration** (sensor-to-sensor alignment)

### 2. Temporal Filtering Stage

#### 2.1 Statistical Stability Filter
**Based on:** SARndbox FrameFilter with GPU acceleration

**Algorithm Enhancements:**
- **Adaptive window size** based on motion detection
- **Per-region variance thresholds** for different sand areas
- **Predictive hysteresis** using motion vectors
- **Multi-resolution processing** for efficiency

**GPU Implementation:**
```glsl
// Compute Shader for statistical filtering
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rg32f) uniform image2D currentFrame;
layout(binding = 1, rg32f) uniform image2D historyBuffer;
layout(binding = 2, rgba32f) uniform image2D statisticsBuffer; // mean, variance, count, lastValid

uniform float hysteresisThreshold;
uniform int minSamples;
uniform float maxVariance;

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    
    // Load current depth and statistics
    vec2 current = imageLoad(currentFrame, coord).xy;
    vec4 stats = imageLoad(statisticsBuffer, coord);
    
    // Update running statistics
    // ... statistical stability algorithm ...
    
    // Apply hysteresis and output
    // ... hysteresis filtering ...
}
```

#### 2.2 Motion-Aware Processing
**Innovation:** Detect and adapt to different motion patterns

**Features:**
- **Static region detection** - higher stability for unchanging areas
- **Active region tracking** - faster response in areas being modified
- **Motion vector estimation** - predict changes for smoother updates
- **Confidence mapping** - per-pixel stability confidence

### 3. Spatial Filtering Stage

#### 3.1 Edge-Preserving Filter
**Enhancement over legacy:** Anisotropic filtering instead of simple low-pass

**Algorithm Options:**
- **Bilateral filter** - preserve edges while smoothing noise
- **Anisotropic diffusion** - flow-aware smoothing
- **Non-local means** - pattern-based denoising
- **Guided filter** - fast edge-preserving alternative

**Adaptive Selection:**
```cpp
class SpatialFilterManager {
    enum FilterType { BILATERAL, ANISOTROPIC, NON_LOCAL_MEANS, GUIDED };
    
    FilterType selectOptimalFilter(const FrameAnalysis& analysis);
    void applyFilter(FilterType type, const Frame& input, Frame& output);
};
```

#### 3.2 Multi-Resolution Processing
**Purpose:** Efficient processing of different detail levels

**Pyramid Levels:**
- **Level 0** (full res): Edge detection and fine details
- **Level 1** (1/2 res): Medium-scale smoothing
- **Level 2** (1/4 res): Large-scale trend analysis
- **Level 3** (1/8 res): Global motion estimation

### 4. World Frame Generation

#### 4.1 Multi-Sensor Fusion
**Innovation:** Combine data from multiple depth sensors

**Fusion Strategies:**
- **Confidence-weighted averaging** - blend based on sensor reliability
- **Geometric consensus** - use multiple viewpoints for accuracy
- **Temporal consistency** - maintain coherence across sensors
- **Occlusion handling** - deal with sensor shadowing

**Data Structure:**
```cpp
struct WorldFrame {
    HeightMap heightMap;
    ConfidenceMap confidenceMap;
    VelocityField velocityField;
    ObjectList detectedObjects;
    EventList recentEvents;
    uint64_t frameId;
    uint64_t timestamp;
    SensorMask activeSensors;
};
```

#### 4.2 Height Map Generation
**Purpose:** Convert processed depth data to elevation above base plane

**Features:**
- **Automatic base plane update** - adapt to sand settling
- **Multi-level height encoding** - support for overhangs and tunnels
- **Boundary condition handling** - smooth edges and invalid regions
- **Quality metrics** - per-region data quality assessment

---

## Parameter Management System

### 1. Configuration Profiles
**Purpose:** Different settings for different sandbox configurations

**Profile Types:**
- `StandardSandbox` - typical single Kinect setup
- `LargeSandbox` - multi-sensor installation
- `HighPrecision` - research/demonstration mode
- `LowLatency` - real-time performance priority
- `Custom` - user-defined parameters

### 2. Adaptive Parameter Tuning
**Innovation:** Automatic optimization based on runtime conditions

**Adaptation Strategies:**
- **Noise level monitoring** - adjust filtering strength
- **Motion pattern analysis** - tune temporal parameters
- **Performance monitoring** - balance quality vs. speed
- **User activity detection** - increase responsiveness during interaction

**Example Implementation:**
```cpp
class AdaptiveParameterManager {
    struct ParameterSet {
        float hysteresisThreshold;
        int temporalWindowSize;
        float spatialFilterStrength;
        int processingResolution;
    };
    
    void updateParameters(const FrameAnalytics& analytics);
    ParameterSet getCurrentParameters() const;
    void saveOptimalParameters(const std::string& configName);
};
```

---

## Real-time Diagnostics and Visualization

### 1. Processing Pipeline Visualization
**Purpose:** Real-time debugging and parameter tuning

**Diagnostic Outputs:**
- **Per-stage frame visualization** - see intermediate results
- **Statistical heatmaps** - variance, confidence, motion
- **Performance metrics** - timing, memory usage, GPU utilization
- **Quality assessment** - noise levels, stability metrics

### 2. Interactive Parameter Tuning
**Features:**
- **Real-time sliders** - adjust parameters while running
- **A/B comparison mode** - compare different settings
- **Automatic benchmarking** - measure quality improvements
- **Configuration export** - save optimal settings

---

## Integration with Caldera Architecture

### 1. HAL Layer Interface
**Data Flow:** HAL → Processing → Transport

**Interface Design:**
```cpp
class ProcessingLayer {
public:
    void initialize(const ProcessingConfig& config);
    void processRawFrame(const RawDataPacket& rawData);
    void registerWorldFrameCallback(std::function<void(const WorldFrame&)> callback);
    
    // Real-time parameter adjustment
    void updateParameters(const std::string& paramName, float value);
    ProcessingStatistics getStatistics() const;
};
```

### 2. Transport Layer Integration
**Output:** Clean WorldFrame objects for Unity frontend

**Quality Assurance:**
- **Frame validation** - ensure data integrity
- **Fallback mechanisms** - handle sensor failures gracefully
- **Bandwidth optimization** - compress data for network transport
- **Synchronization** - coordinate with frontend frame rate

### 3. Configuration Management
**Integration with existing config system:**
- **Environment variable overrides** - `CALDERA_PROCESSING_*`
- **Runtime reconfiguration** - update without restart
- **Profile switching** - change modes dynamically
- **Calibration integration** - coordinate with sensor setup

---

## Performance Targets and Optimization

### 1. Performance Goals
- **Latency:** < 16ms total processing time (60 FPS)
- **Throughput:** Support up to 4x Kinect sensors simultaneously
- **Memory:** < 500MB GPU memory footprint
- **CPU Usage:** < 20% on modern quad-core system

### 2. Optimization Strategies
- **Compute shader parallelization** - leverage thousands of GPU cores
- **Memory pooling** - minimize allocations in hot path
- **Pipeline batching** - process multiple frames efficiently
- **LOD processing** - reduce quality for distant/stable regions

### 3. Scalability Considerations
- **Multi-GPU support** - distribute sensors across GPU cards
- **Network distribution** - process on separate machines
- **Dynamic quality scaling** - adapt to available resources
- **Graceful degradation** - maintain functionality under load

---

## Future Enhancement Opportunities

### 1. Machine Learning Integration
- **Learned noise models** - train on specific sandbox setups
- **Predictive filtering** - anticipate user actions
- **Automatic calibration** - self-tuning parameters
- **Anomaly detection** - identify unusual patterns

### 2. Advanced Algorithms
- **Temporal super-resolution** - interpolate between frames
- **Multi-modal fusion** - combine depth with color/IR
- **Physics-informed filtering** - use sand physics for validation
- **Compressed sensing** - recover data from sparse measurements

### 3. Extended Sensor Support
- **LiDAR integration** - high-precision scanning
- **Stereo vision** - passive depth estimation
- **Time-of-flight cameras** - alternative depth sensing
- **Structured light** - custom projection patterns