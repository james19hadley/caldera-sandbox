# Processing Layer Implementation Working Plan (Assistant Draft)

Last Updated: 2025-10-05
Owner: AI assistant (implementation companion)
Scope: Concrete, incremental plan to evolve the current processing layer from basic depth-to-height conversion to a robust, multi-stage, validated, adaptive perception pipeline.

## 1. Guiding Principles
- Keep frame hot-path allocation-free (reuse buffers, pre-size vectors)
- Make every stage individually testable (unit + integration harness)
- Fail fast on configuration mismatch (dimension, calibration profile)
- Separate HARD invalidation (geometry/range) from SOFT instability (temporal variance)
- Provide introspection hooks early (stats, counters, sampling logs)
- Defer GPU port until CPU correctness + profiling baseline established
- Support per-sensor isolation first; fusion is an explicit, pluggable stage (avoid premature interleaving)
- Treat defective / unstable pixels as dynamic phenomena (process-time classification, not static calibration-only)

## 2. Current Baseline (State Assessment)
| Component | Status | Notes |
|-----------|--------|-------|
| DepthCorrector | Basic placeholder | Uniform radial factor only, no per-pixel table load |
| CoordinateTransform | Implemented + tested | Supports intrinsics + base plane; fusion scaffold present |
| TemporalFilter | Production | Mirrors SARndbox statistics / hysteresis semantics (world-space heights) |
| Spatial Filtering | Implemented (Phase 1) | Separable [1 2 1] kernel, NaN-aware, env gated `CALDERA_ENABLE_SPATIAL_FILTER` / adaptive gating in M4 |
| Plane Validation | Implemented (world-space) | Zero-depth reject + min/max plane bounds (env fallback + profile override); image-space parity deferred |
| Invalid Pixel Metrics | Active | Valid/invalid counts logged per frame (used in tests) |
| ProcessingManager | Enhanced | Point cloud build, plane validation, temporal + optional spatial, fusion passthrough, stability metrics, adaptive mode, profile autoload |
| Diagnostics | Instrumented | Stage timing + variance/stability metrics behind `CALDERA_PROCESSING_STABILITY_METRICS` |
| Multi-Sensor Fusion | Phase 0 (scaffold) | `FusionAccumulator` passthrough; future min-z / confidence weighting pending |

## 3. Target Incremental Milestones

### M1: Hard Invalid Handling + PointCloud Path (Week 0+)
Goal: Introduce geometric validation & per-pixel validity propagation before temporal filtering.
Status (2025-10-05 FINAL):
- World-space point cloud build with zero-depth rejection.
- Plane-based validation using `TransformParameters.minValidPlane/maxValidPlane`.
- Environment elevation overrides (`CALDERA_ELEV_MIN_OFFSET_M`, `CALDERA_ELEV_MAX_OFFSET_M`).
- TemporalFilter integration over constructed height map.
- Tests: `test_processing_invalid_pixels`, `test_processing_plane_validation` passing.
- Logging summarizes valid/invalid counts.
Deferred (Phase 1.5): optional image-space validator parity mode.

Legacy Parity Note: SARndbox applies plane clipping in depth image space using `(x+0.5, y+0.5, depthCorrected)` against `minPlane` / `maxPlane`. Phase 1.5 may add image-space validation for parity.

### M2: Spatial Low-Pass Filter (Classic)
Goal: Reduce residual speckle after temporal stabilization.
Steps:
1. Implement `SpatialFilter` (vertical then horizontal pass; border clamp).
2. Interface: Reuse `IHeightMapFilter`.
3. Optional toggle via env `CALDERA_ENABLE_SPATIAL_FILTER`.
4. Unit test: Impulse noise reduction.
5. Integration test: Temporal + Spatial chaining.

Status (2025-10-05 COMPLETED Phase 1):
- Implemented separable [1 2 1] kernel (`SpatialFilter`) with NaN-aware weight renormalization.
- Tests: impulse spread + NaN skip logic (`SpatialFilterTest.*`).
- Integrated behind `CALDERA_ENABLE_SPATIAL_FILTER` (static env) and now also indirectly via M4 adaptive gating.
Deferred:
- Integration chain test (temporal+spatial) until adaptive hysteresis solidified.
- SIMD optimization after profiling.

### M3: Calibration Profile Enrichment
Goal: Move plane params + depth scale & min/max range to loaded profile.

Status (2025-10-05 COMPLETED Phase 1):
 - Profile autoload (`CALDERA_CALIB_SENSOR_ID`, optional `CALDERA_CALIB_DIR`) overrides env plane fallback (`CALDERA_CALIB_MIN_PLANE` / `CALDERA_CALIB_MAX_PLANE`).
 - Plane bounds now serialized/deserialized in profile JSON.
 - Tests: `EnvCalibrationFallbackTest.AppliesEnvPlanes`, `CalibrationProfileLoadingTest.AppliesProfilePlanesOverFallback` validate fallback and precedence.
 - Stability metrics instrumentation (timings + variance/stability ratio) added concurrently (see §6) with tests `StabilityMetricsTest.*`.
Deferred:
 - Depth scale override if/when stored in profile.
 - Integration test chaining calibrated planes + spatial filter (after adaptive hysteresis to avoid churn).
 - Optional additional env variable `CALDERA_FALLBACK_PLANES` style consolidated parser (currently separate min/max vars).

### M4: Adaptive Filtering (Spatial + Temporal Hooks)
Goal: Dynamically enable / strengthen spatial smoothing only when stability is poor; introduce foundation for temporal aggressiveness scaling without always-on blur.

Status (2025-10-05 FINAL):
IMPLEMENTED (Phase 1 + Phase 2 complete):
* Adaptive spatial gating (mode 2) using prior frame stability / variance thresholds.
* Hysteresis streak control (`CALDERA_ADAPTIVE_ON_STREAK`, `CALDERA_ADAPTIVE_OFF_STREAK`).
* Strong mode escalation (double-pass classic) governed by `CALDERA_ADAPTIVE_STRONG_MULT`, `CALDERA_ADAPTIVE_STRONG_STAB_FRACTION`, `CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS`.
* Alternative kernel experiment (wide5 `[1 4 6 4 1]`) via `CALDERA_SPATIAL_KERNEL_ALT=wide5` retained as optional (not default). Decision: baseline = classic single-pass; strong = classic double-pass (wide5 stays experimental until edge metric or visual review justifies promotion).
* Sampling variance effectiveness metric `spatialVarianceRatio` (sampled pre/post) behind metrics gating.
* Extended metrics: `adaptiveSpatial`, `adaptiveStrong`, `adaptiveStreak`, plus `adaptiveTemporalBlend` for new temporal hook.
* Adaptive temporal scaling hook: optional blend factor when unstable controlled by `CALDERA_ADAPTIVE_TEMPORAL_SCALE` (applies additional smoothing blend to stabilize transition frames). Test: `AdaptiveTemporalScaleTest.BlendsWhenUnstable`.
* Logging for enable/disable/strong transitions.
* All adaptive + sampling + kernel tests passing: `AdaptiveSpatialTest.*`, `AdaptiveSpatialPhase2Test.*`, `SpatialSamplingMetricTest`, `SpatialKernelWide5Test.*`, `AdaptiveTemporalScaleTest.*`.

Deferred (M4-scoped) / intentionally omitted:
* Edge preservation gradient metric (may be added later if kernel promotion reconsidered).
* Diagnostic export channel (ring buffer) beyond logging.
* `CALDERA_ADAPTIVE_STRONG_KERNEL` override (not needed after deciding on classic double-pass default; could be added trivially if requirements change).

Related deferrals tracked under M3 or future milestones: depth scale override; consolidated plane fallback parser.

### M4: Adaptive Parameter Hooks (Foundation)
Goal: Runtime tuning scaffolding (no algorithmic complexity yet).

### M5: Confidence Map (Derived Surface)
Goal: Emit per-pixel confidence (0..1 float) enabling downstream visualization, filtering, and fusion weighting.

Conceptual Inputs:
1. Validity (hard invalid mask) – zero-confidence if geometrically invalid.
2. Local temporal stability – inverse relation to recent per-pixel temporal variance (currently we track frame-level variance; per-pixel temporal stats future optional enhancement). For MVP we proxy via global stabilityRatio & avgVariance modulated by pixel validity.
3. Spatial agreement (optional future) – could reflect local gradient consistency or neighborhood variance; deferred.
4. Adaptive state influence – when adaptive spatial engaged, ephemeral confidence dip can be smoothed to avoid flicker.

MVP Formula (Frame-Scalar Assisted):
Let:
	V = 1 if pixel valid else 0.
	S = clamp(stabilityRatio, 0, 1).
	R = spatialVarianceRatio (<=1 if spatial helped; use 1 if unavailable or spatial disabled).
	T = adaptiveTemporalBlend (0..1; 1 means extra temporal blending applied this frame) – treat high T as increasing confidence that noise is being managed, but cap effect.

Proposed confidence c per pixel (MVP, using frame-level scalars):
	c = V * ( wS * S + wR * (1 - min(1, R)) + wT * T ) / (wS + wR + wT)
Recommended weights initial: wS=0.6, wR=0.25, wT=0.15 (empirical tuning later). If spatial disabled, omit wR term and renormalize.

Clamping & Edge Conditions:
* If frame stabilityRatio or avgVariance unavailable (metrics disabled), default S=0.5.
* If adaptiveTemporalBlend not engaged, T=0.
* If pixel later gets per-pixel temporal stats, replace S with per-pixel stability.

Data Structure:
* New float buffer `confidenceMap_` aligned with height map dimensions; reused per frame.
* Populated post-spatial (so it reflects final smoothing state) before fusion.
* Exported optionally behind `CALDERA_ENABLE_CONFIDENCE_MAP` (env, default 0). When disabled, buffer not allocated / not written.

Metrics & Logging:
* Frame-level aggregates: meanConfidence, fractionLow (<0.3), fractionHigh (>0.8) when enabled (optional metrics flag reuse).
* Add to `StabilityMetrics` struct only aggregates (not full map) to avoid footprint.

Future Enhancements (defer):
* Per-pixel temporal EMA variance to refine S locally.
* Edge preservation adjustment: down-weight confidence on high local gradient spikes coupled with high temporal variance (potential shimmer).
* Confidence decay for persistently invalid → recently revalidated transitions.
* Fusion weighting: incorporate into M6 Phase 2 as weights for multi-sensor blending.

Testing Plan (Immediate):
1. Unit test: invalid pixels yield confidence 0.
2. When stabilityRatio high and spatialVarianceRatio shows improvement (R<1), confidence increases vs synthetic noisy unstable frame.
3. Adaptive temporal engaged increases mean confidence moderately (within defined cap) vs same frame without T.
4. Disabled path (env off) leaves metrics aggregates zero/unset.

Risks:
* Using frame-level scalars for all pixels may mask localized instability → acceptable for MVP; document and iterate.
* Weight tuning may require empirical adjustments; keep env overrides for wS,wR,wT (optional env: `CALDERA_CONFIDENCE_WEIGHTS` "wS,wR,wT").

Acceptance Criteria MVP:
* Confidence map allocation only when enabled.
* All MVP tests passing.
* No added allocations in steady-state hot path beyond buffer reuse.
* Mean confidence monotonic with increasing stabilityRatio in synthetic tests.

### M6: Multi-Sensor Fusion (Foundational Architecture)
Goal: Allow injecting second sensor path (mock) and fusing.
Precursor (early): Add no-op `FusionAccumulator` now so later stages don’t require refactoring.
Phase Breakdown:
1. Phase 0 (Now): Scaffold class with `beginFrame`, `addLayer`, `fuse` (passthrough if single layer).
2. Phase 1: Implement min-z strategy for >1 layer.
3. Phase 2: Confidence-weighted average (after M5).
4. Phase 3: Timeout & sensor dropout handling + metrics.
5. Phase 4: Pluggable strategies (function objects / enum dispatch).
Tests: passthrough -> overlap(min) -> weighted -> dropout.

### M7+: Advanced (Deferred)
- Motion field estimation
- GPU ports (profiling-driven)
- Dead pixel static map generation
- Inpainting for invalid holes
- Legacy image-space plane parity harness
- Dynamic defective pixel map builder
- Adaptive per-sensor confidence weighting

## 4. Proposed Interfaces / Sketches
```cpp
struct StageTimings { uint64_t tDepthUS=0, tTransformUS=0, tValidateUS=0, tTemporalUS=0, tSpatialUS=0, tTotalUS=0; };
```

## 5. Testing Matrix (Initial Focus)
| Test | Type | Purpose |
|------|------|---------|
| test_processing_invalid_pixels_basic | Unit | Range + plane invalidation accuracy |
| test_processing_invalid_pixels_legacy_mode (phase 1.5) | Unit | Image vs world parity |
| test_processing_temporal_stability_static | Existing | Regression guard |
| test_processing_spatial_filter_impulse | Unit | Kernel correctness |
| test_processing_adaptive_window | Unit | Adaptive tuning logic |
| test_processing_confidence_variance | Unit | Confidence mapping validity |
| test_processing_fusion_min | Unit | Min-z fusion correctness |
| test_processing_pipeline_chain | Integration | End-to-end full stack |

Deferred Addition (planned, not yet implemented):
| test_processing_temporal_spatial_chain | Integration | Verify combined Temporal+Spatial preserves stability & reduces speckle |

## 6. Metrics & Instrumentation Plan
Counters: invalidPixelsFrame, stabilityRatio, avgVariance, processingTimeTotalUS, stage timings, fusionOverheadUS.

Stability Delta Instrumentation (Planned M2 extension):
- Purpose: quantify improvement from each filter stage (e.g., variance reduction ratio, fraction of pixels with |delta| < epsilon).
- Approach: sample (not full frame) a fixed-size stratified subset (e.g., 1024 indices) before & after each filter.
- Metrics stored in ring buffer length N (e.g., 120) for rolling averages.
- Overhead control: gated by env `CALDERA_PROCESSING_STABILITY_METRICS=1` (default off). When off, zero cost beyond static flag check.
- Data points per sampled frame:
	* preTemporalVariance, postTemporalVariance
	* preSpatialVariance, postSpatialVariance (if spatial enabled)
	* stablePixelRatioBefore/After (|delta| < eps)
- Export path: aggregated every 120 frames in single log line or exposed via future diagnostics API.

## 7. Risk Register (Brief)
| Risk | Impact | Mitigation |
|------|--------|-----------|
| Divergence from legacy plane semantics | Subtle diffs | Optional image-space mode |
| NaN propagation in filters | Artifact streaks | Explicit NaN handling path |
| Fusion timing mismatch | Ghosting | Add sync window + timeout |
| Buffer allocation churn | Latency | Pre-size & reuse |
| Over-optimization early | Wasted time | Profile after M2 |

## 8. Environment & Feature Flags (Proposed)
| Env Var | Effect | Default |
|---------|--------|---------|
| CALDERA_ENABLE_SPATIAL_FILTER | Enable SpatialFilter stage | 0 |
| CALDERA_SPATIAL_KERNEL | Kernel variant | classic |
| CALDERA_TEMPORAL_ADAPTIVE | Adaptive window tuning | 0 |
| CALDERA_PROCESSING_TRACE | Verbose invalid pixel trace | 0 |
| CALDERA_VALIDATE_IMAGE_SPACE | Legacy image-space plane clipping | 0 |

## 9. Immediate Next Action
1. Implement Confidence Map MVP (buffer allocation, per-pixel fill using frame-level scalars + weights env parsing, aggregates).
2. Add tests: invalid->0, stability influence, spatial improvement influence, adaptive temporal influence (gated), disabled path no-op.
3. Extend `StabilityMetrics` with meanConfidence / fractionLow / fractionHigh (only when confidence enabled) + log line.
4. Optional: introduce `CALDERA_CONFIDENCE_WEIGHTS` parsing now for flexibility.
5. (Deferred after MVP) Consider edge gradient sampling; only proceed if kernel promotion decision revisited.

## 10. Open Questions
1. Export confidence map now or defer?
2. Need per-sensor extrinsics this quarter?
3. Raw saturation heuristic?
4. SIMD validation worth early effort?

## 11. Changelog (Working)
- 2025-10-05: Initial draft.
- 2025-10-05: Added legacy plane analysis, risk matrix, fusion addendum.
- 2025-10-05: Updated M1 to FINAL; added early FusionAccumulator phased plan.
- 2025-10-05: Implemented Phase 0 FusionAccumulator scaffold + ProcessingManager integration + passthrough test.
- 2025-10-05: M2 started: SpatialFilter added (NaN-aware), impulse & NaN tests, env toggle integration.
- 2025-10-05: M2 Phase 1 completed; deferred integration chain + SIMD optimization.
- 2025-10-05: M3 Phase 1 completed (profile autoload, plane serialization, env fallback precedence); stability metrics instrumentation + tests.
- 2025-10-05: M4 Phase 1 adaptive spatial gating implemented + test passing.
- 2025-10-05: M4 Phase 2 partial: hysteresis + strong (double-pass) mode implemented; metrics extended; phase2 tests added; adaptive base test updated to 3-frame logic.
- 2025-10-05: Added sampling-based spatial effectiveness metric (spatialVarianceRatio) + tests.
- 2025-10-05: Added alternative wide5 spatial kernel experiment under env flag + tests.
- 2025-10-05: M4 finalized (adaptive spatial + temporal hook + metrics). Wide5 remains experimental; classic double-pass is strong mode default.
- 2025-10-05: Drafted M5 Confidence Map specification (MVP formula, inputs, testing plan).

## 12. Multi-Sensor Architecture (Design Addendum)
(unchanged from prior draft; retained for context)

## 13. Dead / Defective Pixel Handling Rationale
(unchanged; retained)

## 14. Ideas / Enhancements Log
(unchanged; retained)

(End of working plan)
