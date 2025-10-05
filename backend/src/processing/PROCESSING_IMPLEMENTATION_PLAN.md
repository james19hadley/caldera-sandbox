# Processing Layer Implementation Working Plan (Assistant Draft)

Last Updated: 2025-10-05
Owner: AI assistant (implementation companion)
Scope: Concrete, incremental plan to evolve the current processing layer from basic depth-to-height conversion to a robust, multi-stage, validated, adaptive perception pipeline.

---
## 1. Guiding Principles
- Keep frame hot-path allocation-free (reuse buffers, pre-size vectors)
- Make every stage individually testable (unit + integration harness)
- Fail fast on configuration mismatch (dimension, calibration profile)
- Separate HARD invalidation (geometry/range) from SOFT instability (temporal variance)
- Provide introspection hooks early (stats, counters, sampling logs)
- Defer GPU port until CPU correctness + profiling baseline established
- Support per-sensor isolation first; fusion is an explicit, pluggable stage (avoid premature interleaving)
- Treat defective / unstable pixels as dynamic phenomena (process-time classification, not static calibration-only)

---
## 2. Current Baseline (State Assessment)
| Component | Status | Notes |
|-----------|--------|-------|
| DepthCorrector | Basic placeholder | Uniform radial factor only, no per-pixel table load |
| CoordinateTransform | Implemented + tested | Supports intrinsics + base plane; no fusion yet |
| TemporalFilter | Production | Mirrors SARndbox statistics / hysteresis semantics (world-space heights) |
| Spatial Filtering | Not started | Planned simple 2-pass [1 2 1] kernel first (legacy parity) |
| Plane Validation | Partial | World-space planes only; legacy used image-space equations (see §4) |
| Invalid Pixel Metrics | Stub | Counters defined; not incremented |
| ProcessingManager | Minimal | Depth->height scaling, missing point cloud + validation stage |
| Diagnostics | Minimal | Periodic logs; no per-stage timing instrumentation |
| Multi-Sensor Fusion | Not started | Will require alignment transform + blend policies |

---
## 3. Target Incremental Milestones

### M1: Hard Invalid Handling + PointCloud Path (Week 0+)
Goal: Introduce geometric validation & per-pixel validity propagation before temporal filtering.
Status (2025-10-05):
- Implemented world-space point cloud build + zero-depth rejection + basic z-range placeholder.
- Integrated TemporalFilter over constructed height map.
- Added test `test_processing_invalid_pixels` (passes) validating counting logic.
- Logging provides valid/invalid counts periodically.
Pending for full M1 completion:
- Replace placeholder z-range with calibration-derived base plane + elevation offsets.
- Add env overrides `CALDERA_ELEV_MIN_OFFSET_M` / `CALDERA_ELEV_MAX_OFFSET_M`.
- Add plane validation unit test.

Legacy Parity Note: SARndbox applies plane clipping in depth image space using `(x+0.5, y+0.5, depthCorrected)` against `minPlane` / `maxPlane` that were pre-transformed from camera space. Our Phase 1 implementation will do validation in world space (post transform) for simplicity. A Phase 1.5 enhancement (optional) can add an image-space validation mode reproducing legacy equations exactly.

Steps:
1. Add internal conversion RawDepthFrame -> InternalPointCloud (with x,y placeholder or derived from intrinsics later).
2. Apply depth range + plane tests (world-space) -> mark Point3D.valid=false; set z=NaN.
3. Increment `ProcessingStats.invalidPixelsSkipped`.
4. Feed point cloud into `TemporalFilter::processFrame()`; maintain last valid height.
5. Reconstruct `WorldFrame.heightMap` from filtered cloud (invalid => previous stable or instableValue logic already handled by filter).
6. Unit test: craft synthetic RawDepthFrame with out-of-range & plane-violating depths -> expect skipped counts.

Phase 1.5 (Optional):
 - Add `ImageSpacePlaneValidator` that stores legacy-style `minPlaneImage[4]`, `maxPlaneImage[4]` and evaluates directly on `(px, py, rawOrCorrectedDepth)` before world transform.
 - Benchmark difference vs world-space (expected negligible for near-orthogonal setups).

Acceptance: Stability unaffected for valid pixels; invalid count matches expectation; no crashes.

### M2: Spatial Low-Pass Filter (Classic) (Next)
Goal: Reduce residual speckle after temporal stabilization.

Steps:
1. Implement `SpatialFilter` (vertical then horizontal pass; border clamp).
2. Interface: Reuse `IHeightMapFilter`.
3. Optional toggle via config/env (e.g. `CALDERA_ENABLE_SPATIAL_FILTER`).
4. Unit test: Impulse noise (salt-and-pepper) -> reduction in local variance.
5. Integration test: TemporalFilter + SpatialFilter chaining (ensure order correct & no dimension mismatch).

### M3: Calibration Profile Enrichment
Goal: Move plane params + depth scale & min/max range to loaded profile.

Steps:
1. Extend existing calibration loader (SensorCalibration) to expose minPlane/maxPlane & depthRange.
2. Validate mismatched sensor dimension early.
3. Provide CLI/log dump on load.
4. Unit test: Profile with extreme ranges rejects invalid points.

### M4: Adaptive Parameter Hooks (Foundation)
Goal: Expose runtime tuning interface without internal algorithm changes yet.

Steps:
1. Add `ProcessingAdaptiveController` with snapshot of rolling metrics (stability ratio, invalid ratio, processing time).
2. Every N frames evaluate thresholds (e.g. if stability > 95% for 300 frames -> allow reducing temporal window; else increase).
3. Provide no-op default; feature flag to enable.
4. Unit test: Simulated metric feed adjusts target window size.

### M5: Confidence Map (Derived Surface)
Goal: Produce per-pixel confidence (0..1) for frontend (optional transport extension later).

Definition (first pass):
`confidence = clamp(1 - (variance / maxVariance), 0, 1) * (isStable?1:0)`

Steps:
1. Add optional buffer parallel to height map.
2. Populate post temporal filter phase.
3. Expose via ProcessingManager debugging API (not yet in WorldFrame contract).
4. Unit test: High variance -> near 0; stable near mean -> near 1.

### M6: Multi-Sensor Fusion (Foundational Architecture)
Goal: Allow injecting second sensor path (mock) and fusing.

Steps:
1. Create `FusionAccumulator` collecting `InternalPointCloud` per sensor frame id.
2. Basic strategy 1: Chosen depth = min(z) of valid points (simplest occlusion-safe).
3. Strategy 2 (configurable): Weighted average by provisional confidence.
4. Unit test: Overlapping regions -> min vs average produce expected results.

### M7+: Advanced (Deferred)
- Motion field estimation (optical flow approximation or gradient-of-difference).
- GPU ports (compute shader path; profiling-driven selection of kernels).
- Dead pixel static map generation (persist & reload).
- Inpainting for invalid holes (edge-aware).
 - Optional legacy plane emulation mode (image-space clipping parity test harness).
- Dynamic defective pixel map builder (periodically flag persistent instability)
- Per-sensor adaptive confidence weighting (variance-driven fusion)

---
## 4. Proposed Interfaces / Sketches

```cpp
// In ProcessingManager.h additions
struct StageTimings { uint64_t tDepthUS=0, tTransformUS=0, tValidateUS=0, tTemporalUS=0, tSpatialUS=0, tTotalUS=0; };

class ProcessingManager {
  // ...existing code...
private:
  InternalPointCloud cloudIn_;
  InternalPointCloud cloudFiltered_;
  std::unique_ptr<SpatialFilter> spatialFilter_; // lazy init
  StageTimings lastTimings_;
  ProcessingStats stats_;
};
```

Validation helper (inline or separate):
```cpp
inline bool isDepthRawInvalid(uint16_t d) { return d == 0; /* extend with sensor saturation threshold */ }
```

---
## 5. Testing Matrix (Initial Focus)
| Test | Type | Purpose |
|------|------|---------|
| test_processing_invalid_pixels_basic | Unit | Range + plane invalidation accuracy |
| test_processing_invalid_pixels_legacy_mode (phase 1.5) | Unit | Image-space plane vs world-space parity |
| test_processing_temporal_stability_static | Existing | Regression guard |
| test_processing_spatial_filter_impulse | Unit | Kernel correctness |
| test_processing_adaptive_window | Unit | Parameter auto-adjust logic |
| test_processing_confidence_variance | Unit | Confidence mapping validity |
| test_processing_fusion_min | Unit | Multi-sensor fusion min strategy |
| test_processing_pipeline_chain | Integration | End-to-end pass with all enabled |

---
## 6. Metrics & Instrumentation Plan
Counters:
- invalidPixelsFrame (per frame)
- stabilityRatio (temporal)
- avgVariance
- processingTimeTotalUS
- temporalTimeUS / spatialTimeUS / validationTimeUS
- fusionOverheadUS (later)

Logging cadence:
- Every 60 frames: compact summary (ratios + timings)
- Debug on demand: set `CALDERA_PROCESSING_TRACE=1` -> dumps first N invalid coordinates sample.

---
## 7. Risk Register (Brief)
| Risk | Impact | Mitigation |
|------|--------|-----------|
| Early over-optimization (GPU too soon) | Wasted time | CPU correctness first, profile after M2 |
| Temporal filter misuse with NaNs proliferating | Stale artifacts | Force NaN -> retainValids path consistent |
| Fusion timing mismatch (sensor drift) | Ghosting | Later: time-sync & frame alignment window |
| Memory growth (buffers multiplied per stage) | Latency / leaks | Pre-size & reuse; add memory tests (already present) |
| Divergence from legacy plane semantics | Subtle behavioral diffs | Add optional image-space validator & comparison test |

---
## 8. Environment & Feature Flags (Proposed)
| Env Var | Effect | Default |
|---------|--------|---------|
| CALDERA_ENABLE_SPATIAL_FILTER | Enable SpatialFilter stage | 0 |
| CALDERA_SPATIAL_KERNEL | Select kernel variant (classic,bilateral) | classic |
| CALDERA_TEMPORAL_ADAPTIVE | Enable adaptive window tuning | 0 |
| CALDERA_PROCESSING_TRACE | Verbose debug for invalid pixels | 0 |
| CALDERA_VALIDATE_IMAGE_SPACE | Enable legacy-style image-space plane clipping (phase 1.5) | 0 |

---
## 9. Immediate Next Action
Implement Milestone M1 (world-space validation): integrate invalid pixel handling & point cloud path inside `ProcessingManager` + unit test `test_processing_invalid_pixels_basic`. Prepare scaffolding hook for optional image-space mode (flag only, stub implementation).

---
## 10. Open Questions
1. Should we export confidence map now or defer until contract version bump?
2. Will multi-sensor fusion require per-sensor extrinsic load pipeline this quarter?
3. Acceptable heuristic for raw saturation detection (sensor-specific)?
4. Do we want a fast SIMD pass for validation (width divisible by 8/16)?

---
## 11. Changelog (Working)
- 2025-10-05: Initial draft created.
 - 2025-10-05: Added legacy plane clipping analysis & optional image-space validation phase; expanded risk & test matrix.
- 2025-10-05: Added multi-sensor per-sensor pipeline + fusion notes; rationale for processing-stage dead pixel handling.

---
## 12. Multi-Sensor Architecture (Design Addendum)

### 12.1 Pipeline Topology

Per-sensor independent pipelines feed a fusion stage:

```
RawDepthFrame(S1) --> [Correction] --> [Transform] --> [Validation] --> [Temporal/Spatial] --> HeightMap S1
RawDepthFrame(S2) --> [Correction] --> [Transform] --> [Validation] --> [Temporal/Spatial] --> HeightMap S2
...                                                                                         ...
                                          FUSION ACCUMULATOR  <= (registered per-sensor outputs)
                                                     |
                                                     v
                                            Fused WorldFrame (heightMap + optional confidence)
```

### 12.2 Fusion Stage Responsibilities
1. Spatial alignment (assumes transforms already yield same world-space frame)
2. Conflict resolution for overlapping pixels:
   - Strategy 1 (MVP): min(z) of valid pixels (robust for occlusions)
   - Strategy 2: confidence-weighted average (after M5 confidence map)
   - Strategy 3 (future): Bayesian fusion using per-sensor noise models
3. Accumulate per-sensor metadata: active sensors mask, per-sensor coverage ratio, invalid ratio.

### 12.3 Fused Height Map Assembly
Internal representation candidate:
```
struct SensorLayer {
  int sensorIndex;
  const float* heightData; // stabilized
  const float* confidenceData; // optional (nullable)
  float varianceEstimate; // coarse global variance
};

class FusionAccumulator {
 public:
   void beginFrame(uint64_t frameId);
   void addSensorLayer(const SensorLayer& layer);
   void fuse(FusedHeightMap& out); // chooses strategy based on config/env
};
```

### 12.4 Ordering & Latency Considerations
- Fusion waits until either: (a) all expected sensors reported, or (b) timeout (graceful degradation).
- Late sensor: last good layer reused N frames (configurable) before being dropped and sensor flagged inactive.

### 12.5 Configuration Hooks
```
CALDERA_FUSION_EXPECTED_SENSORS=kinect-v1,kinect-v2
CALDERA_FUSION_TIMEOUT_MS=8
CALDERA_FUSION_STRATEGY=min|min_confidence|weighted
CALDERA_FUSION_SENSOR_DROP_FRAMES=90
```

### 12.6 Roadmap Impact
- Introduce FusionAccumulator skeleton at M6 even with single sensor (no-op pass-through) to avoid later invasive refactor.

---
## 13. Dead / Defective Pixel Handling Rationale

### 13.1 Why Not Only Calibration-Time?
- Some pixels degrade over runtime (thermal drift, intermittent dropouts).
- Environment-dependent IR interference can cause transient invalid zones.
- Mechanical shifts (slight tripod bump) alter projection geometry → reclassification needed live.

### 13.2 Layered Approach
| Layer | Purpose | Persistence |
|-------|---------|------------|
| Hard Validation (current M1) | Geometric & range rejection | Immediate, frame-local |
| Temporal Stability (current) | Short-term noise smoothing | Sliding window |
| Defective Map Builder (future) | Identify chronic instability | Aggregated minutes |
| Confidence Map (M5) | Soft weighting for fusion/physics | Per-frame recalculated |

### 13.3 Future Defective Map Heuristic (Sketch)
Pixel flagged defective if over a rolling window W (e.g. 300 frames):
```
instabilityRatio > 0.9 AND variance > highVarianceThreshold
```
Persist snapshot to disk on graceful shutdown; reload to seed initial mask (optional).

---
## 14. Ideas / Enhancements Log (Running)
| Idea | Status | Notes |
|------|--------|-------|
| Hybrid image-space + world-space validation mode | Planned (Phase 1.5) | Env flag CALDERA_VALIDATE_IMAGE_SPACE |
| Per-sensor independent pipelines + central fusion | Scheduled (M6) | Skeleton early to de-risk |
| Confidence-driven fusion | After M5 | Needs variance→confidence mapping |
| Dynamic defective pixel map | Deferred | Requires long-horizon stats buffer |
| Fusion timeout & sensor dropout recovery | Planned M6 | Avoid blocking world frame emission |
| Pluggable fusion strategies | Planned | Strategy pattern or function object |
| Logging compression (adaptive interval) | Backlog | Reduce log noise at stability plateau |
| SIMD validation pass | Backlog | After hotspot profiling |
| GPU temporal filter | Deferred | Only post CPU stability benchmark |

---
(End of working plan)
