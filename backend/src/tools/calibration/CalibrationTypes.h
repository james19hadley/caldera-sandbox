#ifndef CALDERA_BACKEND_TOOLS_CALIBRATION_CALIBRATIONTYPES_H
#define CALDERA_BACKEND_TOOLS_CALIBRATION_CALIBRATIONTYPES_H

#include <vector>
#include <string>
#include <chrono>
#include <cmath>

namespace caldera::backend::tools::calibration {

// 3D point in sensor coordinate space
struct Point3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    
    Point3D() = default;
    Point3D(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// 2D point in image coordinate space
struct Point2D {
    int x = 0;
    int y = 0;
    
    Point2D() = default;
    Point2D(int x_, int y_) : x(x_), y(y_) {}
};

// Plane equation: ax + by + cz + d = 0
struct PlaneEquation {
    float a = 0.0f;  // Normal vector X component
    float b = 0.0f;  // Normal vector Y component  
    float c = 1.0f;  // Normal vector Z component (default points up)
    float d = 0.0f;  // Distance from origin
    
    // Check if point is above plane (positive), on plane (zero), or below (negative)
    float evaluatePoint(const Point3D& point) const {
        return a * point.x + b * point.y + c * point.z + d;
    }
    
    // Get distance from point to plane
    float distanceToPoint(const Point3D& point) const {
        float norm = std::sqrt(a*a + b*b + c*c);
        if (norm == 0.0f) return 0.0f;
        return std::abs(evaluatePoint(point)) / norm;
    }
};

// Calibration data collected during plane measurement
struct PlaneCalibrationData {
    std::string sensorId;
    std::chrono::system_clock::time_point timestamp;
    
    // Collected 3D points for plane fitting
    std::vector<Point3D> collectedPoints;
    
    // Corresponding 2D image coordinates (for validation)
    std::vector<Point2D> imagePoints;
    
    // Fitted plane equation
    PlaneEquation basePlane;
    
    // Validation/quality metrics
    float avgDistanceToPlane = 0.0f;    // Average distance of points to fitted plane
    float maxDistanceToPlane = 0.0f;    // Maximum distance of points to fitted plane
    float planeFitRSquared = 0.0f;      // R² goodness of fit
    bool isValidCalibration = false;    // Whether calibration meets quality thresholds
};

// Complete calibration profile for a sensor
struct SensorCalibrationProfile {
    std::string sensorId;
    std::string sensorType;  // "kinect_v1", "kinect_v2", etc.
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point lastUpdated;
    
    // Core calibration data
    PlaneCalibrationData basePlaneCalibration;
    
    // Optional: depth correction coefficients (future feature)
    bool hasDepthCorrection = false;
    std::vector<float> depthCorrectionCoeffs;  // Per-pixel or polynomial coefficients
    
    // Optional: intrinsic camera parameters (future feature) 
    bool hasIntrinsicCalibration = false;
    float focalLengthX = 0.0f;
    float focalLengthY = 0.0f;
    float principalPointX = 0.0f;
    float principalPointY = 0.0f;
    
    // Validation bounds for processing
    PlaneEquation minValidPlane;  // Points above this are valid
    PlaneEquation maxValidPlane;  // Points below this are valid
};

// Result of calibration operation
enum class CalibrationResult {
    Success,
    InsufficientPoints,    // Not enough points collected
    InsufficientData,      // Not enough points collected (legacy alias)
    PoorPlaneFit,          // Points don't fit to a plane well
    SensorNotAvailable,    // Sensor device unavailable
    InvalidSensorData,     // Sensor providing bad data
    SensorError,           // Sensor error (alias for SensorNotAvailable)
    UserCancelled,         // User cancelled calibration process
    Cancelled,             // User cancelled (alias)
    IOError               // Failed to save/load calibration data
};

// Configuration for calibration process
struct CalibrationConfig {
    // Point collection settings
    int minPointsRequired = 20;           // Minimum points needed for valid calibration
    int maxPointsToCollect = 100;         // Maximum points to collect
    float pointSpacingThreshold = 0.05f;  // Min distance between collected points (meters)
    
    // Quality thresholds  
    float maxAvgDistanceToPlane = 0.02f;  // Max average distance for valid plane (2cm) 
    float maxDistanceThreshold = 0.05f;   // Max any point distance for valid plane (5cm) - CLI alias
    float maxDistanceToPlane = 0.05f;     // Max any point distance for valid plane (5cm)
    float minPlaneFitRSquared = 0.60f;    // Minimum R² for valid plane fit (relaxed for real conditions)
    float rSquaredThreshold = 0.60f;      // Minimum R² for valid plane fit - CLI alias
    
    // Interactive collection settings
    float pointCollectionRadius = 0.1f;   // Radius for interactive point collection (meters)
    
    // Validation plane setup (relative to base plane)
    float minPlaneOffsetMeters = -0.20f;  // 20cm below base (deep sand)
    float maxPlaneOffsetMeters = 0.30f;   // 30cm above base (hand/objects)
};

} // namespace caldera::backend::tools::calibration

#endif // CALDERA_BACKEND_TOOLS_CALIBRATION_CALIBRATIONTYPES_H