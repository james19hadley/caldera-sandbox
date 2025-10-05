# Processing Layer Implementation Working Plan (Assistant Draft)

Last Updated: 2025-10-05 (post legacy removal – unified stage execution baseline)
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

## 2. Current Baseline (State Assessment – Post Legacy Removal)
| Component | Status | Notes |
|-----------|--------|-------|
| DepthCorrector | Basic placeholder | Uniform radial factor only, no per-pixel table load |
| CoordinateTransform | Implemented + tested | Supports intrinsics + base plane; fusion scaffold present |
| TemporalFilter | Production | Mirrors SARndbox statistics / hysteresis semantics (world-space heights) |
| Spatial Filtering | Implemented (Phase 1 + alt kernels) | Separable [1 2 1] kernel, NaN-aware, alt kernels: wide5 + fastgauss (env selectable), adaptive gating in M4 |
| Plane Validation | Implemented (world-space) | Zero-depth reject + min/max plane bounds (env fallback + profile override); image-space parity deferred |
| Invalid Pixel Metrics | Active | Valid/invalid counts logged per frame (used in tests) |
| ProcessingManager | Unified stage path | Legacy branch removed; stage list always active (lambda placeholders for build/spatial/fusion); metrics + adaptive temporal blend still post-loop |
| Diagnostics | Instrumented | Stage timing + variance/stability metrics behind `CALDERA_PROCESSING_STABILITY_METRICS` |
| Multi-Sensor Fusion | Phase 2 (confidence-weighted) | Min-z + confidence-weighted fusion implemented; metrics + fallbacks + duplicate layer test path |

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

### M4: Adaptive Filtering (Spatial + Temporal Hooks + Edge Metric)
Goal: Dynamically enable / strengthen spatial smoothing only when stability is poor; introduce foundation for temporal aggressiveness scaling without always-on blur.

Status (2025-10-05 FINAL):
IMPLEMENTED (Phase 1 + Phase 2 complete):
* Adaptive spatial gating (mode 2) using prior frame stability / variance thresholds.
* Hysteresis streak control (`CALDERA_ADAPTIVE_ON_STREAK`, `CALDERA_ADAPTIVE_OFF_STREAK`).
* Strong mode escalation (double-pass classic) governed by `CALDERA_ADAPTIVE_STRONG_MULT`, `CALDERA_ADAPTIVE_STRONG_STAB_FRACTION`, `CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS`.
* Alternative kernel experiment (wide5 `[1 4 6 4 1]`) via `CALDERA_SPATIAL_KERNEL_ALT=wide5` retained as optional (not default). Decision: baseline = classic single-pass; strong = classic double-pass (wide5 stays experimental; fastgauss added later – see below for edge metric validation).
* Sampling variance effectiveness metric `spatialVarianceRatio` (sampled pre/post) behind metrics gating.
* Edge preservation gradient sampling metric `spatialEdgePreservationRatio` implemented (sampled on same deterministic subset) enabling quantitative comparison of kernels (classic/wide5/fastgauss + strong variants).
* Adaptive temporal scaling hook: optional blend factor when unstable controlled by `CALDERA_ADAPTIVE_TEMPORAL_SCALE` (applies additional smoothing blend to stabilize transition frames). Test: `AdaptiveTemporalScaleTest.BlendsWhenUnstable`.
* Performance benchmark harness (lightweight) added: `SpatialKernelBenchmark.BasicComparativeTiming` measuring ms/frame + variance & edge ratios across kernels.
* Extended metrics: `adaptiveSpatial`, `adaptiveStrong`, `adaptiveStreak`, plus `adaptiveTemporalBlend` for new temporal hook.
* Adaptive temporal scaling hook: optional blend factor when unstable controlled by `CALDERA_ADAPTIVE_TEMPORAL_SCALE` (applies additional smoothing blend to stabilize transition frames). Test: `AdaptiveTemporalScaleTest.BlendsWhenUnstable`.
* Logging for enable/disable/strong transitions.
* All adaptive + sampling + kernel tests passing: `AdaptiveSpatialTest.*`, `AdaptiveSpatialPhase2Test.*`, `SpatialSamplingMetricTest`, `SpatialKernelWide5Test.*`, `AdaptiveTemporalScaleTest.*`.

Deferred (M4-scoped) / intentionally omitted:
* Diagnostic export channel (ring buffer) beyond logging.
* SIMD acceleration of spatial passes.
* Additional strong-mode heuristics beyond current stability fraction and mult.

FastGaussian & Edge Metric Note:
FastGaussian kernel integration (select via `CALDERA_SPATIAL_KERNEL_ALT=fastgauss` and sigma via `CALDERA_FASTGAUSS_SIGMA`) plus edge preservation metric landed after M5 MVP; confidence formula unchanged but metrics can inform future weighting adjustments (e.g. penalize over-smoothed edges).

### M5: Confidence Map (Derived Surface) – COMPLETE
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

Data Structure (Implemented):
* Float buffer `confidenceMap_` aligned with height map dimensions; reused per frame (allocated lazily when first enabled frame processed).
* Computation occurs AFTER metrics (stabilityRatio, spatialVarianceRatio, adaptiveTemporalBlend) are updated— avoids initial frame always zero.
* Currently internal-only; external export / API accessor deferred to M5 Phase 2 or M6 synergy.
* Guarded by `CALDERA_ENABLE_CONFIDENCE_MAP` (default off) — no steady-state allocations after first enable.

Metrics & Logging (Implemented):
* Aggregates in `StabilityMetrics`: meanConfidence, fractionLowConfidence (<0.3), fractionHighConfidence (>0.8).
* Invalid pixels produce c=0 and contribute to low fraction denominator.
* When disabled: aggregates forced to 0 (explicit, no stale state).

Future Enhancements (defer):
* Per-pixel temporal EMA variance to refine S locally.
* Edge preservation adjustment: down-weight confidence on high local gradient spikes coupled with high temporal variance (potential shimmer).
* Confidence decay for persistently invalid → recently revalidated transitions.
* Fusion weighting: incorporate into M6 Phase 2 as weights for multi-sensor blending.

Testing Implemented:
1. Disabled path zero aggregates.
2. Invalid pixels zero, valid >0.
3. Higher stability on second frame raises confidence.
4. Spatial filtering path (R term active) yields >= confidence compared to run with R suppressed.
5. Temporal blend increases confidence after instability.
All in `ConfidenceMapTest` (5 tests) passing.

Risks:
* Using frame-level scalars for all pixels may mask localized instability → acceptable for MVP; document and iterate.
* Weight tuning may require empirical adjustments; keep env overrides for wS,wR,wT (optional env: `CALDERA_CONFIDENCE_WEIGHTS` "wS,wR,wT").

Acceptance Criteria MVP (Met):
* Allocation only when enabled + reuse.
* All confidence tests green.
* First frame can achieve non-zero confidence if input stable.
* Stability monotonic trend confirmed in test.
Remaining (Phase 2): external exposure, fusion weighting, optional per-pixel temporal variance.

### M6: Multi-Sensor Fusion (Foundational Architecture) – SIMPLIFIED MVP
Goal: Enable basic multi-sensor support with minimal logic for now; advanced fusion, dropout, weights, NaN, confidence, etc. are deferred (see below).

Current implementation (MVP):
- If 1 sensor: passthrough (copy input to output, shape WxH).
- If 2 sensors of same size: concatenate along width (output shape 2W x H, left = sensor 1, right = sensor 2).
- All other cases: not supported (future work).
- No dropout, no weights, no NaN/invalid/metrics/strategy logic yet.

Deferred (future M6/M7):
- Min-z, confidence-weighted, robust fusion strategies
- Sensor dropout/timeout handling
- Per-sensor weights, confidence, NaN/invalid handling
- Pluggable strategy interface, env selection
- Metrics, logging, stats, integration tests
- GPU/parallel path, outlier attenuation, cross-sensor consistency

Documented in code as TODO/FIXME for future extension.
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
| SpatialKernelBenchmark.BasicComparativeTiming | Perf | Kernel timing + variance/edge ratios (lightweight benchmark) |
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

## 8. Environment & Feature Flags (Implemented + Planned)
| Env Var | Effect | Default | Status |
|---------|--------|---------|--------|
| CALDERA_ENABLE_SPATIAL_FILTER | Enable static spatial filter | 0 | Implemented |
| CALDERA_SPATIAL_KERNEL_ALT | Alternative spatial kernel (wide5 / fastgauss) | classic | Implemented (wide5 + fastgauss) |
| CALDERA_ADAPTIVE_MODE | Adaptive spatial gating mode | 0 | Implemented |
| CALDERA_ADAPTIVE_ON_STREAK / OFF_STREAK | Hysteresis thresholds | 2 / 2 | Implemented |
| CALDERA_ADAPTIVE_STRONG_MULT | Strong escalation scale | 2.0 | Implemented |
| CALDERA_ADAPTIVE_STRONG_STAB_FRACTION | Stability fraction gating strong | 0.5 | Implemented |
| CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS | Enable classic double-pass strong | 1 | Implemented |
| CALDERA_ADAPTIVE_TEMPORAL_SCALE | Temporal blend intensity | 0 | Implemented |
| CALDERA_ENABLE_CONFIDENCE_MAP | Enable confidence map | 0 | Implemented |
| CALDERA_CONFIDENCE_WEIGHTS | Override weights ("S=0.6,R=0.25,T=0.15") | unset | Implemented |
| CALDERA_PROCESSING_STABILITY_METRICS | Enable sampling + metrics | 0 | Implemented |
| CALDERA_CALIB_SENSOR_ID / CALDERA_CALIB_DIR | Calibration profile autoload | unset | Implemented |
| CALDERA_ELEV_MIN_OFFSET_M / CALDERA_ELEV_MAX_OFFSET_M | Plane fallback offsets | 0 / 0 | Implemented |
| CALDERA_VALIDATE_IMAGE_SPACE | Legacy image-space clipping | 0 | Planned |
| CALDERA_ADAPTIVE_STRONG_KERNEL | Kernel variant for strong (classic_double|wide5|fastgauss) | classic_double | Implemented |
| CALDERA_FASTGAUSS_SIGMA | Sigma parameter for fastgauss kernel | 1.0 | Implemented |
| CALDERA_PROCESSING_PIPELINE | Declarative stage order (e.g. "temporal,spatial,confidence,fusion") | unset | Implemented (parser + minimal execution + when= gating) |
| CALDERA_PIPELINE_STAGE_OPTS | Per-stage key=val overrides | unset | Deferred (will fold into per-stage params already parsed) |

## 9. Immediate Next Actions (Refreshed Post Legacy Removal)
1. Concrete BuildStage & FusionStage (move logic from manager body into stage objects)
2. MetricsStage (port updateMetrics + confidence map; remove post-loop metrics call)
3. AdaptiveControlStage (gating + strong mode decision + temporal scale flagging)
4. SpatialStage emits sampling stats struct consumed by MetricsStage
5. Parser ordering validation + required subset tests
6. Fusion Phase 3: dropout handling (last-seen tracking, stale exclusion)
7. Fusion Phase 4: strategy abstraction + sensor/global weights parsing
8. Concurrency profiling & potential parallel stage execution

## 10. Confidence-Weighted Fusion Specification (Draft)
Goal: Extend Fusion (Phase 2) to combine multiple sensor layers using per-pixel (or frame-level) confidence values, falling back gracefully when information is missing.

### Inputs
For each layer i (i=1..N):
- Height map H_i (size W*H, float; non-finite => invalid).
- Optional confidence map C_i (size W*H, float in [0,1]; missing => implicitly 1.0).
- Optional global sensor weight G_i (env or config; default 1.0).

### Per-Pixel Weight Computation
Raw weight r_i(p) = G_i * C_i(p) * V_i(p) where V_i(p)=1 if H_i(p) finite else 0.
Normalization: w_i(p) = r_i(p) / (Σ_j r_j(p) + ε).
ε: small constant (1e-12) to avoid division by zero.

Fallback Conditions:
1. If Σ_j r_j(p) == 0 (all invalid or zero confidence) → fallback strategy F:
	- F = MIN_Z if any finite height present.
	- Else F = 0.0 (no data).
2. If only one r_k(p) > 0 → direct copy H_k(p).

### Output Height
H_out(p) = Σ_i w_i(p) * H_i(p) (if Σ r_j(p) > 0) else fallback F described above.

### Output Confidence (Composite)
Option A (simple): C_out(p) = max_i C_i(p) over valid contributors.
Option B (weighted): C_out(p) = Σ_i w_i(p) * C_i(p) (provides smoother gradient; choose B for Phase 2).

### Numerical Stability / Edge Cases
- Clamp all input confidence values to [0,1]; treat NaN conf as 0.
- If H_i(p) is extremely outlier vs median: (Phase 3+) optionally down-weight by robust residual check.
- Keep accumulation in double for Σ r_j and Σ w_i*H_i if W*H large (optional micro-optimization; Phase 2 single precision acceptable initially).

### Metrics Additions
- fusionStrategy: enum { MIN_Z=0, CONF_WEIGHT=1 } selected per frame.
- fractionFallbackMinZ: (#pixels falling back to min-z due to Σ r_j=0 while at least one finite) / totalPixels.
- fractionFallbackEmpty: (#pixels all invalid) / totalPixels.
- averageConfidenceOut.

### API Changes
- Extend `FusionAccumulator::fuse` to (Phase 2) optionally fill an outConfidence buffer with weighted confidence.
- Introduce `setGlobalSensorWeights(const std::vector<float>&)` or accept through ProcessingManager mapping (sensorId -> weight).
- Optional perPixelWeights path (already placeholder) to inject precomputed r_i(p) for experimentation/perf.

### Performance Considerations
- Complexity O(N * P) where P=W*H, N=layerCount.
- For small N (<=4 typical) overhead acceptable; later optimize by contiguous SoA memory for heights across layers and loop unrolling.
- Early exit if Σ r_j == 0 after computing r_i (avoid second loop) — keep branch predictable.

### Validation Tests (Planned)
1. WeightedAverageBasic: two layers, distinct confidences ─ verify weighted interpolation.
2. ZeroConfidenceFallback: confidences all zero at pixel but heights finite ⇒ min-z chosen.
3. AllInvalid: outputs 0 and counts in fractionFallbackEmpty.
4. MixedMissingConfidence: one layer lacks confidence buffer ⇒ treat as 1.0.
5. GlobalSensorWeightAffectsBlend: adjust G_i and verify shift in output.

### Incremental Rollout
Phase 2a: Implement weighted formula only when ≥2 layers and at least one confidence map present; otherwise keep min-z.
Phase 2b: Add metrics + fallback counters.
Phase 2c: Optional residual-based attenuation (stretch goal).

### Open Questions
- Do we expose composite confidence map upstream now or delay until consumer ready? (Current accessor OK.)
- Sensor-specific static reliability weights (persist across frames) — integrate via config or env `CALDERA_FUSION_SENSOR_WEIGHTS="id1:1.0,id2:0.8"`.

This spec finalizes scope for Implementation Task: "Implement Confidence Weight Fusion (Phase 2)".

## 5. Testing Matrix (Initial Focus)
| Test | Type | Purpose |
|------|------|---------|
| test_processing_invalid_pixels_basic | Unit | Range + plane invalidation accuracy |
| test_processing_invalid_pixels_legacy_mode (phase 1.5) | Unit | Image vs world parity |
| test_processing_temporal_stability_static | Existing | Regression guard |
| test_processing_spatial_filter_impulse | Unit | Kernel correctness |
| test_processing_adaptive_window | Unit | Adaptive tuning logic |
| test_processing_confidence_variance | Unit | Confidence mapping validity |
| SpatialKernelBenchmark.BasicComparativeTiming | Perf | Kernel timing + variance/edge ratios (lightweight benchmark) |
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

## 8. Environment & Feature Flags (Implemented + Planned)
| Env Var | Effect | Default | Status |
|---------|--------|---------|--------|
| CALDERA_ENABLE_SPATIAL_FILTER | Enable static spatial filter | 0 | Implemented |
| CALDERA_SPATIAL_KERNEL_ALT | Alternative spatial kernel (wide5 / fastgauss) | classic | Implemented (wide5 + fastgauss) |
| CALDERA_ADAPTIVE_MODE | Adaptive spatial gating mode | 0 | Implemented |
| CALDERA_ADAPTIVE_ON_STREAK / OFF_STREAK | Hysteresis thresholds | 2 / 2 | Implemented |
| CALDERA_ADAPTIVE_STRONG_MULT | Strong escalation scale | 2.0 | Implemented |
| CALDERA_ADAPTIVE_STRONG_STAB_FRACTION | Stability fraction gating strong | 0.5 | Implemented |
| CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS | Enable classic double-pass strong | 1 | Implemented |
| CALDERA_ADAPTIVE_TEMPORAL_SCALE | Temporal blend intensity | 0 | Implemented |
| CALDERA_ENABLE_CONFIDENCE_MAP | Enable confidence map | 0 | Implemented |
| CALDERA_CONFIDENCE_WEIGHTS | Override weights ("S=0.6,R=0.25,T=0.15") | unset | Implemented |
| CALDERA_PROCESSING_STABILITY_METRICS | Enable sampling + metrics | 0 | Implemented |
| CALDERA_CALIB_SENSOR_ID / CALDERA_CALIB_DIR | Calibration profile autoload | unset | Implemented |
| CALDERA_ELEV_MIN_OFFSET_M / CALDERA_ELEV_MAX_OFFSET_M | Plane fallback offsets | 0 / 0 | Implemented |
| CALDERA_VALIDATE_IMAGE_SPACE | Legacy image-space clipping | 0 | Planned |
| CALDERA_ADAPTIVE_STRONG_KERNEL | Kernel variant for strong (classic_double|wide5|fastgauss) | classic_double | Implemented |
| CALDERA_FASTGAUSS_SIGMA | Sigma parameter for fastgauss kernel | 1.0 | Implemented |
| CALDERA_PROCESSING_PIPELINE | Declarative stage order (e.g. "temporal,spatial,confidence,fusion") | unset | Implemented (parser + minimal execution + when= gating) |
| CALDERA_PIPELINE_STAGE_OPTS | Per-stage key=val overrides | unset | Deferred |

## 9. Immediate Next Actions (Refreshed Post FastGauss / Edge Metric / Strong Kernel)
1. Stage orchestration Phase 1: parse `CALDERA_PROCESSING_PIPELINE`, execute temporal/spatial with gating (DONE)
2. Confidence externalization: public accessor + optional export hook (log or diagnostic API). Defer shared memory until fusion weighting spec finalized.
3. Fusion Phase 1 (min-z) implementation + tests; integrate with existing FusionAccumulator scaffold.
4. Fusion Phase 2 design draft: confidence-weighted blending strategy (requires map exposure) – prepare spec before coding.
5. Documentation sync: update README / design docs describing new env flags (fastgauss, strong kernel selection, edge metric) and performance benchmark usage.
6. Optional: Add CSV export / multi-iteration mode to `SpatialKernelBenchmark` (env `CALDERA_BENCH_ITER`) for more stable timing distributions.
7. Investigate per-pixel temporal variance (memory layout & incremental update cost model) – decide go/no-go for Confidence Phase 2.

## 5. Testing Matrix (Initial Focus)
| Test | Type | Purpose |
|------|------|---------|
| test_processing_invalid_pixels_basic | Unit | Range + plane invalidation accuracy |
| test_processing_invalid_pixels_legacy_mode (phase 1.5) | Unit | Image vs world parity |
| test_processing_temporal_stability_static | Existing | Regression guard |
| test_processing_spatial_filter_impulse | Unit | Kernel correctness |
| test_processing_adaptive_window | Unit | Adaptive tuning logic |
| test_processing_confidence_variance | Unit | Confidence mapping validity |
| SpatialKernelBenchmark.BasicComparativeTiming | Perf | Kernel timing + variance/edge ratios (lightweight benchmark) |
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

## 8. Environment & Feature Flags (Implemented + Planned)
| Env Var | Effect | Default | Status |
|---------|--------|---------|--------|
| CALDERA_ENABLE_SPATIAL_FILTER | Enable static spatial filter | 0 | Implemented |
| CALDERA_SPATIAL_KERNEL_ALT | Alternative spatial kernel (wide5 / fastgauss) | classic | Implemented (wide5 + fastgauss) |
| CALDERA_ADAPTIVE_MODE | Adaptive spatial gating mode | 0 | Implemented |
| CALDERA_ADAPTIVE_ON_STREAK / OFF_STREAK | Hysteresis thresholds | 2 / 2 | Implemented |
| CALDERA_ADAPTIVE_STRONG_MULT | Strong escalation scale | 2.0 | Implemented |
| CALDERA_ADAPTIVE_STRONG_STAB_FRACTION | Stability fraction gating strong | 0.5 | Implemented |
| CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS | Enable classic double-pass strong | 1 | Implemented |
| CALDERA_ADAPTIVE_TEMPORAL_SCALE | Temporal blend intensity | 0 | Implemented |
| CALDERA_ENABLE_CONFIDENCE_MAP | Enable confidence map | 0 | Implemented |
| CALDERA_CONFIDENCE_WEIGHTS | Override weights ("S=0.6,R=0.25,T=0.15") | unset | Implemented |
| CALDERA_PROCESSING_STABILITY_METRICS | Enable sampling + metrics | 0 | Implemented |
| CALDERA_CALIB_SENSOR_ID / CALDERA_CALIB_DIR | Calibration profile autoload | unset | Implemented |
| CALDERA_ELEV_MIN_OFFSET_M / CALDERA_ELEV_MAX_OFFSET_M | Plane fallback offsets | 0 / 0 | Implemented |
| CALDERA_VALIDATE_IMAGE_SPACE | Legacy image-space clipping | 0 | Planned |
| CALDERA_ADAPTIVE_STRONG_KERNEL | Kernel variant for strong (classic_double|wide5|fastgauss) | classic_double | Implemented |
| CALDERA_FASTGAUSS_SIGMA | Sigma parameter for fastgauss kernel | 1.0 | Implemented |
| CALDERA_PROCESSING_PIPELINE | Declarative stage order (e.g. "temporal,spatial,confidence,fusion") | unset | Implemented |
| CALDERA_PIPELINE_STAGE_OPTS | Per-stage key=val overrides | unset | Deferred |

## 9. Immediate Next Actions (Refreshed Post FastGauss / Edge Metric / Strong Kernel)
1. Stage orchestration Phase 1: parse `CALDERA_PROCESSING_PIPELINE`, temporal/spatial ordering + gating (DONE)
2. Confidence externalization: public accessor + optional export hook (log or diagnostic API). Defer shared memory until fusion weighting spec finalized.
3. Fusion Phase 1 (min-z) implementation + tests; integrate with existing FusionAccumulator scaffold.
4. Fusion Phase 2 design draft: confidence-weighted blending strategy (requires map exposure) – prepare spec before coding.
5. Documentation sync: update README / design docs describing new env flags (fastgauss, strong kernel selection, edge metric) and performance benchmark usage.
6. Optional: Add CSV export / multi-iteration mode to `SpatialKernelBenchmark` (env `CALDERA_BENCH_ITER`) for more stable timing distributions.
7. Investigate per-pixel temporal variance (memory layout & incremental update cost model) – decide go/no-go for Confidence Phase 2.

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
- 2025-10-05: Implemented M5 confidence MVP (buffer, weights parsing, post-metrics computation) + 5 passing tests.
- 2025-10-05: Added plan for FastGaussianBlur integration + stage orchestration and kernel selection flags.
- 2025-10-05: Integrated FastGaussianBlur selectable kernel (env `CALDERA_SPATIAL_KERNEL_ALT=fastgauss`, sigma via `CALDERA_FASTGAUSS_SIGMA`), added edge preservation metric (`spatialEdgePreservationRatio`).
- 2025-10-05: Added adaptive strong kernel selection env (`CALDERA_ADAPTIVE_STRONG_KERNEL`) supporting classic_double|wide5|fastgauss with tests.
- 2025-10-05: Added performance benchmark `SpatialKernelBenchmark.BasicComparativeTiming` to light test target (timing + var/edge ratios).
- 2025-10-05: Fusion Phase 1 (min-z) + metrics + tests (`test_fusion_min_z.cpp`, `test_fusion_metrics.cpp`).
- 2025-10-05: Fusion Phase 2 (confidence-weighted) implementation + tests (`test_fusion_weighted.cpp`, `test_fusion_zero_confidence.cpp`, `test_fusion_all_invalid.cpp`, `test_fusion_confidence_clamp.cpp`); added fallback counters & strategy logging.
- 2025-10-05: Introduced pipeline stage instantiation (BuildStage, TemporalStage, SpatialStage, FusionStage) — unified stage-driven execution active; legacy path removed.
