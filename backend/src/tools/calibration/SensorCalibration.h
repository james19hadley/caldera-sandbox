#pragma once

#include "CalibrationTypes.h"
#include "../../hal/ISensorDevice.h"
#include "../../hal/KinectV1_Device.h"
#include "../../hal/KinectV2_Device.h"
#include "../../common/Logger.h"
#include <memory>
#include <functional>
#include <filesystem>
#include <fstream>

namespace caldera::backend::tools::calibration {

/**
 * Unified sensor calibration system.
 * Handles calibration data collection, processing, and persistence for any ISensorDevice.
 */
class SensorCalibration {
public:
    SensorCalibration();
    ~SensorCalibration() = default;

    // === Sensor Management ===
    
    /**
     * Create sensor device by ID
     * @param sensorId "kinect-v1", "kinect-v2", etc.
     * @return Sensor device or nullptr if unsupported
     */
    std::shared_ptr<hal::ISensorDevice> createSensorDevice(const std::string& sensorId);
    
    /**
     * List available sensor types
     * @return Vector of supported sensor IDs
     */
    std::vector<std::string> getAvailableSensorTypes() const;
    
    // === Calibration Collection ===
    
    /**
     * Collect calibration points automatically from sensor
     * @param sensor Sensor device to use
     * @param config Calibration parameters
     * @param result Output calibration data
     * @return Calibration result status
     */
    CalibrationResult collectAutomaticCalibration(std::shared_ptr<hal::ISensorDevice> sensor,
                                                  const CalibrationConfig& config,
                                                  PlaneCalibrationData& result);
    
    /**
     * Start interactive calibration session
     * @param sensor Sensor device to use
     * @return Success/failure
     */
    bool startInteractiveCalibration(std::shared_ptr<hal::ISensorDevice> sensor);
    
    /**
     * Capture single calibration point during interactive session
     * @param point Output 3D point
     * @return Success/failure
     */
    bool captureCalibrationPoint(Point3D& point);
    
    /**
     * Finish interactive calibration and compute plane
     * @param config Calibration parameters
     * @param result Output calibration data
     * @return Calibration result status
     */
    CalibrationResult finishInteractiveCalibration(const CalibrationConfig& config,
                                                   PlaneCalibrationData& result);
    
    /**
     * Stop interactive calibration session
     */
    void stopInteractiveCalibration();
    
    // === Profile Management ===
    
    /**
     * Save calibration profile to disk
     * @param profile Calibration profile to save
     * @return Success/failure
     */
    bool saveCalibrationProfile(const SensorCalibrationProfile& profile);
    
    /**
     * Load calibration profile from disk
     * @param sensorId Sensor ID to load
     * @param profile Output calibration profile
     * @return Success/failure
     */
    bool loadCalibrationProfile(const std::string& sensorId, SensorCalibrationProfile& profile);
    
    /**
     * Check if calibration profile exists
     * @param sensorId Sensor ID to check
     * @return True if profile exists
     */
    bool hasCalibrationProfile(const std::string& sensorId) const;
    
    /**
     * List all available calibration profiles
     * @return Vector of sensor IDs with profiles
     */
    std::vector<std::string> listCalibrationProfiles() const;
    
    /**
     * Delete calibration profile
     * @param sensorId Sensor ID to delete
     * @return Success/failure
     */
    bool deleteCalibrationProfile(const std::string& sensorId);
    
    // === Validation & Testing ===
    
    /**
     * Validate existing calibration with live sensor data
     * @param sensorId Sensor ID to validate
     * @param numTestPoints Number of test points to collect
     * @return Validation result (avg distance to calibrated plane)
     */
    float validateCalibration(const std::string& sensorId, int numTestPoints = 10);
    
    // === Configuration ===
    
    /**
     * Set calibration storage directory
     * @param directory Directory path for calibration files
     */
    void setCalibrationDirectory(const std::string& directory);
    
    /**
     * Get default calibration configuration
     * @return Default configuration
     */
    CalibrationConfig getDefaultConfig() const;

private:
    // State management
    std::shared_ptr<hal::ISensorDevice> currentSensor_;
    std::shared_ptr<spdlog::logger> logger_;
    std::string calibrationDirectory_;
    
    // Interactive calibration state
    std::vector<Point3D> interactivePoints_;
    common::RawDepthFrame latestDepthFrame_;
    common::RawColorFrame latestColorFrame_;
    bool frameReceived_ = false;
    
    // Frame processing callback
    void onFrameReceived(const common::RawDepthFrame& depth, const common::RawColorFrame& color);
    
    // Utility methods
    bool convertDepthToWorld(int imageX, int imageY, Point3D& worldPoint) const;
    bool fitPlaneToPoints(const std::vector<Point3D>& points, 
                         PlaneEquation& plane, 
                         float& avgDistance, 
                         float& maxDistance, 
                         float& rSquared) const;
    bool validateCalibrationQuality(const PlaneCalibrationData& data, 
                                   const CalibrationConfig& config) const;
    
    // File I/O helpers
    std::string getProfileFilename(const std::string& sensorId) const;
    bool ensureCalibrationDirectoryExists() const;
    std::string serializeProfile(const SensorCalibrationProfile& profile) const;
    bool deserializeProfile(const std::string& jsonData, SensorCalibrationProfile& profile) const;
};

} // namespace caldera::backend::tools::calibration