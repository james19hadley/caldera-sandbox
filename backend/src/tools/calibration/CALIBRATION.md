# Sensor Calibration System

## Overview

The calibration system establishes a reference plane for depth sensors in the AR sandbox environment. It measures the base sand surface and creates coordinate system mappings for real-time processing.

## Key Components

### SensorCalibration Class
- **Purpose**: Unified calibration interface for all sensor types
- **Location**: `SensorCalibration.h/cpp`
- **Supports**: Kinect v1, Kinect v2, future sensors via ISensorDevice interface

### CalibrationTypes
- **PlaneEquation**: Mathematical plane representation (ax + by + cz + d = 0)
- **CalibrationConfig**: Quality thresholds and collection parameters
- **CalibrationResult**: Success/failure status enumeration

## Calibration Process

1. **Point Collection**: Grid-based sampling (20 points in center region)
2. **Plane Fitting**: Horizontal plane approximation using least squares
3. **Quality Validation**: R² analysis and distance thresholds
4. **Profile Storage**: JSON serialization with timestamps

## Quality Metrics

```cpp
maxAvgDistanceToPlane = 0.02f;    // 2cm average error threshold
maxDistanceToPlane = 0.05f;       // 5cm maximum point deviation  
minPlaneFitRSquared = 0.60f;      // 60% R² minimum for acceptance
```

## Usage

### CLI Tool
```bash
./CalibrationTool calibrate kinect-v1      # Automatic calibration
./CalibrationTool validate kinect-v1       # Validation test
./CalibrationTool show kinect-v1           # Display profile
```

### Programmatic API
```cpp
SensorCalibration calibrator;
auto result = calibrator.performAutomaticCalibration(sensor);
auto profile = calibrator.loadProfile("kinect-v1");
```

## File Format

Calibration profiles stored as JSON in `config/calibration/`:
```json
{
  "sensorId": "kinect-v1",
  "basePlane": { "a": 0, "b": 0, "c": 1, "d": -0.926 },
  "avgDistanceToPlane": 0.0045,
  "planeFitRSquared": 0.65
}
```

## Integration

The ProcessingManager uses calibration data for:
- Height validation (points above base plane)
- Coordinate transformation (pixels to world space)  
- Noise filtering (distance-based culling)