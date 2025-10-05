#pragma once

#include "common/Logger.h"
#include "common/DataTypes.h"
#include "processing/ProcessingTypes.h"
#include "tools/calibration/CalibrationTypes.h"
#include <memory>
#include <spdlog/spdlog.h>

namespace caldera::backend::processing {

/**
 * Transforms coordinates from sensor space to world space using calibration data.
 * Based on SARndbox approach for coordinate system transformation.
 */
class CoordinateTransform {
public:
    CoordinateTransform();
    ~CoordinateTransform() = default;

    /**
     * Load transformation parameters from calibration profile
     */
    bool loadFromCalibration(const tools::calibration::SensorCalibrationProfile& calibProfile);
    
    /**
     * Transform single depth pixel to world coordinates
     * @param pixelX Pixel X coordinate (0-based)
     * @param pixelY Pixel Y coordinate (0-based)  
     * @param depthValue Raw depth value from sensor
     * @return World space point, or invalid point if transformation failed
     */
    common::Point3D transformPixelToWorld(int pixelX, int pixelY, float depthValue) const;
    
    /**
     * Transform depth frame to world coordinates
     * @param depthFrame Input depth frame in sensor space
     * @param worldFrame Output frame with world coordinates
     * @return True if transformation succeeded
     */
    bool transformFrameToWorld(const common::DepthFrame& depthFrame, InternalPointCloud& pointCloud) const;
    
    /**
     * Check if transformer is ready for coordinate transformation
     */
    bool isReady() const { return isConfigured_; }
    
    /**
     * Get sensor ID this transformer is configured for
     */
    const std::string& getSensorId() const { return sensorId_; }

private:
    
    /**
     * Initialize default parameters for specific sensor type
     */
    void initializeDefaultParameters(const std::string& sensorType);
    
    /**
     * Apply intrinsic camera transformation (pixel -> camera coordinates)
     */
    common::Point3D pixelToCameraCoords(int pixelX, int pixelY, float depth) const;
    
    /**
     * Apply extrinsic transformation (camera -> world coordinates)
     */
    common::Point3D cameraToWorldCoords(const common::Point3D& cameraPoint) const;
    
    /**
     * Project point onto base plane if needed
     */
    common::Point3D projectOntoBasePlane(const common::Point3D& worldPoint) const;
    
    std::shared_ptr<spdlog::logger> logger_;
    TransformParameters params_;
    std::string sensorId_;
    bool isConfigured_;
};

} // namespace caldera::backend::processing