#pragma once

#include "common/DataTypes.h"
#include <vector>
#include <chrono>
#include <cstdint>
#include <array>

namespace caldera::backend::processing {

/**
 * Internal point cloud for processing pipeline (not part of external contract)
 */
struct InternalPointCloud {
    uint64_t timestamp_ns = 0;
    int width = 0;
    int height = 0;
    std::vector<common::Point3D> points;
    
    void resize(int w, int h) {
        width = w;
        height = h;
        points.resize(w * h);
    }
    
    void clear() {
        width = 0;
        height = 0;
        points.clear();
    }
};

/**
 * Coordinate transformation parameters (internal to CoordinateTransform)
 */
struct TransformParameters {
    // Camera intrinsic parameters
    float focalLengthX = 0.0f;
    float focalLengthY = 0.0f;
    float principalPointX = 0.0f;
    float principalPointY = 0.0f;
    
    // Base plane equation (ax + by + cz + d = 0)
    float planeA = 0.0f, planeB = 0.0f, planeC = 1.0f, planeD = 0.0f;
    
    // Sensor pose (position and orientation)
    common::Point3D sensorPosition;
    float sensorRotationMatrix[9] = {1,0,0, 0,1,0, 0,0,1}; // Identity matrix
    
    // Sensor-specific scaling and offset
    float depthScale = 0.001f;      // Convert raw depth to meters
    float depthOffset = 0.0f;       // Depth value offset
    
    // Plane-based validation (SARndbox algorithm)
    // Pixel is valid if: minValidPlane(x,y,z) >= 0 && maxValidPlane(x,y,z) <= 0
    std::array<float, 4> minValidPlane = {0.0f, 0.0f, 1.0f, -0.5f};  // z >= 0.5m (above min height)
    std::array<float, 4> maxValidPlane = {0.0f, 0.0f, 1.0f, -2.0f};  // z <= 2.0m (below max height)
    
    /**
     * Validate a 3D point against plane constraints
     * @param x,y,z World coordinates in meters
     * @return true if point is within valid range
     */
    bool validatePoint(float x, float y, float z) const {
        // Apply plane equations: ax + by + cz + d
        float minValue = minValidPlane[0]*x + minValidPlane[1]*y + minValidPlane[2]*z + minValidPlane[3];
        float maxValue = maxValidPlane[0]*x + maxValidPlane[1]*y + maxValidPlane[2]*z + maxValidPlane[3];
        
        // Point is valid if it's on the correct side of both planes
        return (minValue >= 0.0f) && (maxValue <= 0.0f);
    }
};

/**
 * Depth correction profile for a specific sensor
 */
struct CorrectionProfile {
    std::string sensorId;
    std::vector<float> pixelCorrections;  // Per-pixel correction factors
    int width = 0;
    int height = 0;
    bool isValid = false;
    
    // Future extensions:
    // Matrix3f intrinsicMatrix;     // Camera intrinsics  
    // Vector4f distortionCoeffs;    // Radial/tangential distortion
    
    void resize(int w, int h) {
        width = w;
        height = h;
        pixelCorrections.resize(w * h, 1.0f); // Default: no correction
    }
    
    void clear() {
        sensorId.clear();
        pixelCorrections.clear();
        width = 0;
        height = 0;
        isValid = false;
    }
};

/**
 * Processing pipeline configuration parameters
 */
struct ProcessingConfig {
    // Depth correction parameters
    bool enableDepthCorrection = true;
    float maxDepthRange = 4.0f;        // Maximum valid depth in meters
    float minDepthRange = 0.3f;        // Minimum valid depth in meters
    
    // Coordinate transformation parameters
    bool enableCoordinateTransform = true;
    
    // Filtering parameters
    bool enableTemporalFiltering = false;
    int temporalFilterWindow = 5;      // Number of frames for temporal filtering
    float temporalFilterThreshold = 0.01f; // Threshold for change detection
    
    // Height map parameters
    int heightMapWidth = 640;
    int heightMapHeight = 480;
    float heightMapResolution = 0.001f; // Meters per pixel
    
    // Processing quality settings
    enum class QualityLevel {
        LOW,    // Fastest processing, lower quality
        MEDIUM, // Balanced processing
        HIGH    // Highest quality, slower processing
    };
    QualityLevel quality = QualityLevel::MEDIUM;
};

/**
 * Processing pipeline statistics and metrics
 */
struct ProcessingStats {
    // Frame processing metrics
    uint64_t totalFramesProcessed = 0;
    uint64_t validPixelsProcessed = 0;
    uint64_t invalidPixelsSkipped = 0;
    
    // Timing metrics (in microseconds)
    uint64_t avgDepthCorrectionTime = 0;
    uint64_t avgCoordinateTransformTime = 0;
    uint64_t avgTotalProcessingTime = 0;
    
    // Quality metrics
    float avgValidPixelRatio = 0.0f;
    float avgDepthRange = 0.0f;
    
    void reset() {
        totalFramesProcessed = 0;
        validPixelsProcessed = 0;
        invalidPixelsSkipped = 0;
        avgDepthCorrectionTime = 0;
        avgCoordinateTransformTime = 0;
        avgTotalProcessingTime = 0;
        avgValidPixelRatio = 0.0f;
        avgDepthRange = 0.0f;
    }
};

} // namespace caldera::backend::processing