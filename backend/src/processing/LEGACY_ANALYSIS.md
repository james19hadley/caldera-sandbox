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