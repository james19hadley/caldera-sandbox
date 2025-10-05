# Legacy Processing Analysis (SARndbox 2.8)

Date: 2025-10-05
Scope: Compare legacy SARndbox (extras/SARndbox-2.8) filtering & elevation pipeline with current Caldera processing implementation. Identify reusable semantics vs. deliberate divergence and enumerate concrete action items.

## 1. Legacy Pipeline Overview (Derived from `Sandbox.cpp` + `FrameFilter.cpp`)
High-level data flow (runtime threads):
```
Kinect Frame -> FrameFilter (background thread) -> Filtered Frame Queue -> Sandbox foreground rendering -> DEM / Water / Tools
```
Key processing inside `FrameFilter::filterThreadMethod`:
1. Copy new raw depth frame into working slot (with background thread synchronization).
2. Per-pixel loop:
	- Maintain circular "averaging buffer" of last N (numAveragingSlots) raw depth samples per pixel.
	- Maintain statistics per pixel: count, sum, sum of squares (3 x unsigned int) for valid samples only.
	- Depth correction via `PixelDepthCorrection::correct` before plane validity test (pixel + 0.5 center shift).
	- Image-space plane validity test using two plane equations: `minPlane`, `maxPlane` in depth image coordinate system.
	- If valid: add new sample, subtract aged slot sample (if it was valid) to keep rolling stats.
	- Stability criterion: pixel considered stable if `count >= minNumSamples` AND `sPtr[2]*count <= maxVariance*count*count + sum*sum` (variance bound inequality form avoiding division).
	- If stable: update output value with running mean only if absolute delta exceeds `hysteresis` threshold; else retain previous output (temporal smoothing + change suppression).
	- If instable and `retainValids` true: keep previous output; otherwise output `instableValue` (default 0.0).
3. After per-pixel pass: spatial filter (if enabled) = two full passes of a separable 3-tap kernel (vertical then horizontal) with border handling as weighted 2/3 edges.
4. Post filtering: deliver frame to consumer via callback.

## 2. Legacy Concepts Extracted
| Concept | Legacy Implementation | Notes / Parameters |
|---------|-----------------------|--------------------|
| Depth Correction | Per-pixel correction class (`PixelDepthCorrection`) | We currently have a placeholder radial factor; no per-pixel map yet. |
| Validity (Planes) | Image-space min/max planes after transforming base plane | We implemented world-space plane clipping; parity mode pending (image-space). |
| Rolling Statistics | count / sum / sumSq over sliding window of raw depth | Our temporal filter works over world-space height; we log variance proxy via EMA of frame deltas. |
| Stability Criterion | Inequality avoiding float division (variance bound) | Potential reuse for more numerically stable test. |
| Hysteresis (Temporal) | Mean applied only if delta > hysteresis; else hold | We borrowed concept but currently simpler stability ratio + variance threshold. |
| Instable Handling | Optionally hold last good or output default baseline | We treat unstable as trigger for enabling spatial filter (adaptive path). |
| Spatial Filter | Fixed 3x3 separable [1 2 1] with double pass (vertical then horizontal twice overall) | We run single pass currently; strong mode duplicates pass (aligns). |
| Threading | Dedicated filtering thread + frame queue | Not implemented; we process synchronously now. |
| Per-Pixel Running Mean | Stored in `validBuffer` / output frame | We maintain previous world frame heightmap implicitly. |
| Plane Transform | Base plane transformed to depth-image coordinate space | Currently use world-space transform & plane evaluation per point. |

## 3. Mapping to Current Caldera Implementation
| Legacy Feature | Caldera Status | Gap / Decision |
|----------------|---------------|----------------|
| Image-space plane validity | Not yet (world-space only) | Add optional parity mode for regression vs legacy. |
| Sliding window raw depth stats | Not implemented (use temporal smoothing & variance of deltas) | Could add lightweight integer stats for precise variance if needed for confidence map. |
| Variance inequality check | Not implemented | Evaluate for better stable detection vs heuristic thresholds. |
| Hysteresis delta threshold per pixel | Not implemented (we have frame-level stability gating) | Consider local per-pixel hysteresis to avoid flicker on edges. |
| Double-pass spatial when unstable | Implemented as "strong mode" | Matches legacy semantics conceptually. |
| Per-pixel correction map | Placeholder only | Roadmap M3/M7 extension. |
| Background thread filter | Not implemented | Defer until performance profiling (risk of latency). |
| InstableValue default fill | Not needed (we currently keep last or NaN) | Keep divergence unless consumer requires sentinel fill. |

## 4. Reuse vs Re-Design Decisions
| Area | Decision | Rationale |
|------|----------|-----------|
| Stability Criterion | Consider porting inequality form | Avoid division & floating precision issues; matches proven legacy. |
| Per-Pixel Hysteresis | Optional future addition | Could reduce shimmering; implement after confidence map if needed. |
| Image-Space Plane Mode | Will add as flag `CALDERA_VALIDATE_IMAGE_SPACE` | Ensures comparability for regression tests. |
| Double-Pass Strong Mode | Already aligned | Maintain; evaluate multi-kernel only if measurable improvement. |
| Sliding Window Stats | Possibly simplified variant (count, mean, M2) | Lower memory vs full sum of squares; feed confidence map. |
| Threading | Postpone | Complexity not justified until profiling indicates CPU bound. |

## 5. Action Items (Proposed)
Priority (near-term first):
1. Add image-space plane validation optional path (legacy parity) – reuse legacy plane equation transform logic.
2. Introduce reusable inline stability check helper using inequality pattern (wrap current metrics for experimental A/B in tests).
3. Design confidence map using (a) per-pixel mean difference EMA OR (b) sliding window variance inequality classification; choose minimal memory approach.
4. Benchmark strong double-pass vs alternative wider kernel ([1 4 6 4 1]) vs legacy double-pass of [1 2 1]; retain best variance reduction / blur trade-off.
5. Optional: per-pixel hysteresis threshold (use small epsilon, maybe adapt from legacy 0.1f scaled to world units).
6. Document divergence: instable fill strategy & threading (explicitly accepted differences).

## 6. Risk / Compatibility Considerations
- Parity Mode Risk: Additional branch may add small overhead; mitigate by compiling out when not enabled.
- Stability Inequality vs Current Heuristics: Changing activation thresholds could destabilize current adaptive gating; gate behind experimental env variable.
- Wider Kernel: Increases blur footprint; must quantify effect via edge preservation metric (Sobel gradient energy change) before adoption.

## 7. Proposed Env Extensions
| Env | Purpose | Default |
|-----|---------|---------|
| CALDERA_VALIDATE_IMAGE_SPACE | Toggle legacy-style image-space plane clipping | 0 |
| CALDERA_ADAPTIVE_STABILITY_MODE | 0=current heuristic, 1=legacy inequality | 0 |
| CALDERA_SPATIAL_KERNEL_ALT | "classic" / "passthrough" / "wide5" | classic |
| CALDERA_HYSTERESIS_EPS | Per-pixel delta threshold (if enabled) | disabled |

## 8. Data Structure Adjustments
If adopting sliding window stats: store for each pixel:
```
struct PixelStat { uint16_t count; float mean; float m2; } // Welford
```
Memory: 8 bytes/pixel vs legacy 12 bytes (count,sum,sumSq). For 640x480 ≈ 2.46 MB (double-buffer optional).

## 9. Confidence Map Outline (Link to Legacy)
Legacy stability = (count >= minSamples && variance <= threshold). We can map confidence:
```
confidence = clamp( w1 * stableFlag + w2 * (1 - normalizedVariance) + w3 * temporalConsistency , 0, 1 )
```
Where normalizedVariance derived from (m2 / (count-1)) scaled by threshold.

## 10. Summary
Most core adaptive ideas we reintroduced (double-pass, hysteresis concept) are consistent with proven legacy behavior. Immediate value: reintroduce image-space plane option and evaluate legacy variance inequality for more principled stability gating before expanding kernel complexity.

---
(End of LEGACY_ANALYSIS)

# SARndbox-2.8 Data Processing Analysis

## Legacy Project Overview

SARndbox (Augmented Reality Sandbox) version 2.8 is a mature interactive AR sandbox solution by Oliver Kreylos. The project uses Kinect v1 for sand scanning and a projector to display topographic maps and water simulation.

### Legacy Architectural Principles:
- **Monolithic application** (in contrast to our two-process approach)
- **OpenGL + Vrui framework** for rendering and GUI
- **Multi-threaded processing** using producer-consumer patterns
- **GPU acceleration** for water simulation and rendering

---

## Key Data Processing Components

### 1. FrameFilter - Foundation of Stabilization
**Files:** `FrameFilter.h/cpp`

#### Core Principles:
- **Multi-slot averaging** (`numAveragingSlots`) - buffer of N frames per pixel
- **Statistical stability analysis** - mean/variance calculation over time windows
- **Hysteresis filtering** - change threshold to prevent jittering
- **Per-pixel depth correction** - lens distortion compensation

#### Pixel Stabilization Algorithm:
1. **Input validation** - depth range checking via plane equations
2. **Statistics update** - accumulating sum, sum², count for sliding window
3. **Stability test**: `variance*N <= maxVariance*N² + mean²`
4. **Hysteresis**: update only if `|new_mean - old_value| >= threshold`
5. **Retain/Reset policy** for unstable pixels

#### Key Parameters:
- `minNumSamples` - minimum valid samples for stability
- `maxVariance` - maximum variance for stability  
- `hysteresis` - value change threshold (typically 0.1f)
- `retainValids` - whether to retain previous stable values

### 2. Spatial Filtering
**Implementation in FrameFilter::filterThreadMethod()**

#### Two-pass low-pass filter:
- **Vertical pass**: column filtering with weights `[1, 2, 1]/4`
- **Horizontal pass**: row filtering with same weights
- **Boundary conditions**: special handling for edge pixels `[2, 1]/3`

### 3. Depth Data Preprocessing

#### Depth Correction (Per-pixel calibration):
- **Goal**: compensate for Kinect lens optical distortions
- **Method**: measure flat surface from various distances
- **Storage**: correction coefficients per pixel
- **Application**: `corrected_depth = pixelCorrection.correct(raw_depth)`

#### Coordinate Space Transformations:
1. **Raw Depth → Corrected Depth** (per-pixel correction)
2. **Depth Image Space → Camera Space** (intrinsic calibration)
3. **Camera Space → World Space** (extrinsic calibration)

### 4. Data Validation via Plane Equations

#### Principle:
- Define valid range through planes in depth image space
- `minPlane` and `maxPlane` set boundaries for acceptable depth values
- Pixel is valid if: `minPlane(x,y,z) ≥ 0 && maxPlane(x,y,z) ≤ 0`

#### Setup:
- Base plane measured on flat sand using plane extraction tool
- Min/max boundaries set relative to base plane

---

## Calibration and System Setup

### Multi-stage calibration process:
1. **Per-pixel depth correction** - lens distortion correction
2. **Intrinsic camera calibration** - internal camera parameters (optional)
3. **Base plane measurement** - sand base plane equation
4. **Sandbox extents** - 3D coordinates of sandbox corners  
5. **Projector-camera calibration** - projector↔camera transformation matrix

### Critical Setup Requirements:
- **Perpendicular mounting** - Kinect must look straight down
- **Focus calibration** - projector focused on average sand height
- **Full-screen mode** - calibration and operation only in full-screen
- **Stability requirement** - sand must not move during calibration

---

## Performance and Optimizations

### Multi-threaded Architecture:
- **Background filtering thread** - frame processing in background
- **Triple buffering** - lock-free data exchange between threads  
- **Conditional wake-up** - processing only when new frames arrive

### GPU Acceleration:
- **Depth texture storage** on GPU via OpenGL texture rectangles
- **Shader-based rendering** for topography and water
- **Frame buffer objects** for shadow mapping and intermediate passes

### Memory Management:
- **Streaming buffer patterns** - minimize data copying
- **Fixed-size allocations** - avoid dynamic allocations in hot path
- **Circular buffers** for temporal averaging windows

---

## Legacy Approach Problems and Limitations

### 1. Architectural Limitations:
- **Monolithic structure** - difficult to scale and maintain
- **Vrui dependency** - tied to specific framework
- **Single-sensor focus** - optimized for Kinect v1 only

### 2. Performance Bottlenecks:
- **CPU-heavy filtering** - main processing on CPU
- **Memory bandwidth** - frequent CPU-GPU data copying
- **Single-threaded calibration** - slow setup process

### 3. Flexibility Limitations:
- **Hardcoded parameters** - many magic constants in code
- **Limited sensor support** - difficult to add new sensor types
- **Monolithic configuration** - all settings in one place

---

## Key Insights for Our Project

### What Works Excellently and Should Be Adapted:

1. **Statistical stability approach** - mean/variance analysis is very effective
2. **Hysteresis filtering** - critical for preventing jittering
3. **Multi-frame averaging** - essential for noise filtering
4. **Spatial filtering** - simple and effective low-pass filter
5. **Plane-based validation** - elegant way to filter invalid data
6. **Per-pixel depth correction** - mandatory for quality output

### What Can Be Improved in Our Approach:

1. **GPU acceleration** of entire processing pipeline
2. **Modular architecture** with hot-swappable algorithms
3. **Multi-sensor fusion** for multiple Kinect operation
4. **Adaptive parameters** - automatic tuning for conditions
5. **Real-time visualization** of processing pipeline for debugging
6. **Configuration profiles** for different installation types

### Algorithmic Innovations for Caldera:

1. **Edge-preserving filtering** - anisotropic diffusion instead of simple LP
2. **Temporal coherence tracking** - advanced stability models
3. **Multi-resolution processing** - pyramid-based approach for efficiency
4. **Machine learning integration** - trainable noise filters
5. **Predictive filtering** - using previous frames for prediction