# Caldera Processing Pipeline (Stage-Oriented Architecture)

This document explains the purpose, concepts, and current migration state of the processing pipeline refactor.
It is intentionally implementation-focused (not marketing) and lives alongside the source for fast iteration.

## 1. Goals

1. Provide a single unified stage-driven processing path (legacy flow removed) composed of discrete, testable stages.
2. Allow pipeline specification via environment (`CALDERA_PROCESSING_PIPELINE`) for experimentation and tuning without recompilation (with safe defaults when unset).
3. Centralize adaptive behavior (spatial gating, strong variants, temporal blending) and confidence map computation in well-defined stage boundaries.
4. Maintain stable metrics interface while enabling internal stage evolution.
5. Prepare for future multi-sensor orchestration / parallelism by isolating responsibilities and minimizing shared mutable state.

## 2. High-Level Stage Flow (Current Unified Path)
```
RawDepthFrame
  -> build (depth -> point cloud + height map + plane validation)
  -> temporal (optional filter)
  -> spatial (adaptive gating + strong variants + sampling metrics)
  -> adaptive temporal blend (applied post-spatial when unstable)
  -> fusion (single + optional duplicate synthetic layer for weighting tests)
  -> metrics + confidence aggregation
  -> callback(WorldFrame)
```
All computation now conceptually expressed as stages; some logic (adaptive temporal blend + metrics) still executes in manager after stages pending extraction into dedicated stages.

## 3. Stage Model
Each stage implements:
```cpp
struct FrameContext { /* mutable processing buffers + metadata */ };
struct IProcessingStage { virtual void apply(FrameContext&) = 0; };
```
Planned canonical stages (subject to evolution):
- build (depth -> point cloud + height map creation & validation)
- temporal (IIR / custom filter)
- spatial (kernel-based smoothing + sampling metrics)
- adaptive_control (future: decide spatial/strong/temporal scaling BEFORE filters)
- fusion (aggregate layers into final height map)
- metrics (aggregate + confidence map)

Currently lambda-backed stages are instantiated; build and fusion logic still finalize outside the lambda bodies (they act as markers) pending promotion to concrete stage classes. Metrics and adaptive temporal blend are post-stage steps (roadmap below).

## 4. FrameContext (Migration Fields)
Key fields (see `ProcessingStages.h`):
- `std::vector<float> height`     : current mutable height map
- `std::vector<uint8_t> validity` : per-pixel validity flags (1 valid, 0 invalid)
- `std::vector<float>* confidence`: optional confidence map buffer pointer
- `void* metricsOpaque`           : opaque pointer to stability metrics (legacy layout)
- `AdaptiveState adaptive`        : flags & kernel choice for strong spatial filtering
- `uint32_t width/heightPx/frameId` : dimensions + sequence ID
- `const RawDepthFrame* rawDepthFrame` (migration) & `InternalPointCloud* internalCloud`
- book-keeping flags: `spatialApplied`, `fusionCompleted`

## 5. Pipeline Specification
Environment variable: `CALDERA_PROCESSING_PIPELINE`

Syntax (comma-separated stages):
```
CALDERA_PROCESSING_PIPELINE="build,temporal,spatial,fusion"
```
Parameters (prototype):
```
spatial(kernel=fastgauss,sample_count=512)
```
Parser rules:
- Lowercases stage names and parameter keys
- Splits only at top-level commas (nested parentheses allowed in future)
- Valid identifier chars: `[A-Za-z0-9_-]`
- Errors produce a warning and disable stage execution (falls back to legacy path if feature flag disabled)

## 6. Execution Mode
Stage execution is always active (legacy branch removed). If `CALDERA_PROCESSING_PIPELINE` is unset a safe default pipeline is synthesized:
```
build[,temporal_if_filter_injected],spatial,fusion
```
Invalid or empty specs result in falling back to this default. No runtime flag is required to enable stages.

## 7. Metrics & Confidence
A shared helper `ProcessingManager::updateMetrics` now aggregates:
- Timing (build, fuse, total) — filter time currently lumped
- Stability ratio & variance EMA
- Spatial sampling effects (variance/edge ratios) when available
- Adaptive flags (spatial/strong/temporal blend)
- Confidence map (weighted components S,R,T) when enabled

Migration path:
1. Stage path uses same helper for parity.
2. Later: introduce explicit `metrics` stage holding aggregation logic; legacy path removed.

## 8. Adaptive Logic
Adaptive gating is currently computed once per frame before stage loop (populating `FrameContext.adaptive`). Future extraction into an `adaptive_control` stage will allow unit tests around decision boundaries and per-stage timing isolation. `AdaptiveState.strongKernelChoice` continues to guide strong variant selection (classic_double / wide5 / fastgauss).

## 9. Current Limitations / TODO
| Area | Gap | Planned Action |
|------|-----|----------------|
| Build Stage | Logic still outside lambda body | Introduce concrete `BuildStage` moving cloud + height extraction inside stage loop |
| Fusion Stage | Fusion executed after stages | Promote to concrete `FusionStage` performing accumulation + layer duplication internally |
| Metrics Stage | Metrics & confidence after loop | Add `MetricsStage` consuming height & spatial sampling artifacts |
| Adaptive Control | Pre-loop decision inline | Create `AdaptiveControlStage` before temporal/spatial |
| Ordering Validation | Parser accepts arbitrary order | Enforce allowed dependency graph + tests |
| Opaque Metrics Pointer | `void*` in FrameContext | Replace with forward-declared struct once detached from manager private layout |
| Concurrency | Global coarse mutex | Evaluate per-sensor manager instances or stage-level locks post single-sensor correctness |
| Spatial Sampling | Sampling done only in classic path helper | Emit sampling via spatial stage & propagate to metrics stage |

## 10. Testing Strategy
Added / Active tests:
- `test_stage_pipeline_basic.cpp` – stage pipeline smoke (unified path)
- `test_pipeline_parser.cpp` – parser correctness
- Existing processing & fusion tests exercise downstream effects (temporal, spatial, fusion, confidence)

Upcoming tests:
- Stage ordering enforcement / invalid spec rejection
- Metrics stage invariants (timing monotonicity, non-negative counters)
- Adaptive control decision boundaries (hysteresis correctness)

## 11. Roadmap (Updated Post-Legacy Removal)
1. Concrete BuildStage & FusionStage (eliminate post-loop fusion section)
2. MetricsStage (aggregation + confidence) – removes direct call to `updateMetrics` in manager
3. AdaptiveControlStage – isolates gating + strong mode decision & temporal scale flagging
4. SpatialStage enhancement – emit sampling stats into a shared struct consumed by MetricsStage
5. Parser hardening – enforce DAG order + required stage subset validation
6. Error / Recovery stage (optional) – consistent handling of catastrophic frame issues
7. Concurrency exploration – stage-level profiling & potential task dispatch

## 12. Design Principles
- Incremental refactor: maintain working legacy path until parity confirmed
- Zero-cost opt-out: if flag disabled, behavior/overhead unchanged (aside from negligible extra members)
- Observability first: logging & metrics kept stable during transition
- Test-first for structural changes (parser, equivalence) before deep rewrites

## 13. Extending the Pipeline
To add a new stage type:
1. Define a concrete class implementing `IProcessingStage` or use `LambdaStage` for rapid prototyping.
2. Extend `rebuildPipelineStages()` to recognize its name & parameters.
3. Update docs + add a parser test case.
4. Add equivalence / regression tests exercising the new stage in isolation and in sequence.

## 14. Legacy Decommission (Completed)
The legacy branch conditional has been removed; stage execution is the sole path. No fallback flag remains. Next simplification steps will shrink `FrameContext` once metrics & fusion sit fully inside dedicated stages.

---
Questions / proposals: open a PR referencing this file or annotate directly with TODO blocks near the affected section.
