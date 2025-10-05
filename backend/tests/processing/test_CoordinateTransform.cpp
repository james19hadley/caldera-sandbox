#include <gtest/gtest.h>
#include "processing/CoordinateTransform.h"
#include "processing/ProcessingTypes.h"
#include "common/Logger.h"
#include "common/DataTypes.h"
#include "tools/calibration/SensorCalibration.h"
#include <limits>

using namespace caldera::backend::processing;
using namespace caldera::backend::common;
using namespace caldera::backend::tools::calibration;

class CoordinateTransformTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize Logger for all tests
        if (!Logger::instance().isInitialized()) {
            Logger::instance().initialize("logs/test/coordinate_transform.log");
        }
    }
};

TEST_F(CoordinateTransformTest, ConstructorInitializesCorrectly) {
    CoordinateTransform transform;
    
    EXPECT_FALSE(transform.isReady());
    EXPECT_TRUE(transform.getSensorId().empty());
}

TEST_F(CoordinateTransformTest, LoadFromKinectV1Calibration) {
    CoordinateTransform transform;
    
    // Create mock calibration profile for Kinect v1
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -0.5f;  // Base plane at z = 0.5m
    
    bool loaded = transform.loadFromCalibration(calibProfile);
    
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(transform.isReady());
    EXPECT_EQ(transform.getSensorId(), "test-kinect-v1");
}

TEST_F(CoordinateTransformTest, LoadFromKinectV2Calibration) {
    CoordinateTransform transform;
    
    // Create mock calibration profile for Kinect v2
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v2"; 
    calibProfile.sensorType = "kinect-v2";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -0.8f;  // Base plane at z = 0.8m
    
    bool loaded = transform.loadFromCalibration(calibProfile);
    
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(transform.isReady());
    EXPECT_EQ(transform.getSensorId(), "test-kinect-v2");
}

TEST_F(CoordinateTransformTest, LoadFromUnknownSensorType) {
    CoordinateTransform transform;
    
    // Create calibration profile with unknown sensor type
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-unknown";
    calibProfile.sensorType = "unknown-sensor";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -1.0f;
    
    bool loaded = transform.loadFromCalibration(calibProfile);
    
    EXPECT_TRUE(loaded); // Should still load with generic defaults
    EXPECT_TRUE(transform.isReady());
    EXPECT_EQ(transform.getSensorId(), "test-unknown");
}

TEST_F(CoordinateTransformTest, TransformPixelWithoutCalibration) {
    CoordinateTransform transform;
    
    Point3D result = transform.transformPixelToWorld(320, 240, 1000.0f);
    
    EXPECT_FALSE(result.valid);
}

TEST_F(CoordinateTransformTest, TransformCenterPixelWithCalibration) {
    CoordinateTransform transform;
    
    // Load calibration
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = 0.0f;  // Base plane at z = 0
    
    transform.loadFromCalibration(calibProfile);
    
    // Transform center pixel (should be close to optical axis)
    Point3D result = transform.transformPixelToWorld(320, 240, 1000.0f); // 1000mm = 1m depth
    
    EXPECT_TRUE(result.valid);
    EXPECT_NEAR(result.x, 0.0f, 0.01f); // Should be close to center
    EXPECT_NEAR(result.y, 0.0f, 0.01f); // Should be close to center
    EXPECT_GT(result.z, 0.0f); // Should have positive depth in world space
}

TEST_F(CoordinateTransformTest, TransformInvalidDepthValues) {
    CoordinateTransform transform;
    
    // Load calibration
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = 0.0f;
    
    transform.loadFromCalibration(calibProfile);
    
    // Test various invalid depth values
    Point3D result1 = transform.transformPixelToWorld(320, 240, 0.0f);     // Zero depth
    Point3D result2 = transform.transformPixelToWorld(320, 240, -100.0f);  // Negative depth
    Point3D result3 = transform.transformPixelToWorld(320, 240, std::numeric_limits<float>::quiet_NaN());
    Point3D result4 = transform.transformPixelToWorld(320, 240, std::numeric_limits<float>::infinity());
    
    EXPECT_FALSE(result1.valid);
    EXPECT_FALSE(result2.valid);
    EXPECT_FALSE(result3.valid);
    EXPECT_FALSE(result4.valid);
}

TEST_F(CoordinateTransformTest, TransformFrameWithoutCalibration) {
    CoordinateTransform transform;
    
    // Create test depth frame
    DepthFrame depthFrame;
    depthFrame.width = 4;
    depthFrame.height = 4;
    depthFrame.data.resize(16, 1000.0f);
    depthFrame.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    InternalPointCloud pointCloud;
    
    bool result = transform.transformFrameToWorld(depthFrame, pointCloud);
    
    EXPECT_FALSE(result);
}

TEST_F(CoordinateTransformTest, TransformFrameWithCalibration) {
    CoordinateTransform transform;
    
    // Load calibration
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = 0.0f;
    
    transform.loadFromCalibration(calibProfile);
    
    // Create test depth frame with valid data
    DepthFrame depthFrame;
    depthFrame.width = 2;
    depthFrame.height = 2;
    depthFrame.data = {1000.0f, 1100.0f, 1200.0f, 1300.0f}; // All valid depths
    depthFrame.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    InternalPointCloud pointCloud;
    
    bool result = transform.transformFrameToWorld(depthFrame, pointCloud);
    
    EXPECT_TRUE(result);
    EXPECT_EQ(pointCloud.width, 2);
    EXPECT_EQ(pointCloud.height, 2);
    EXPECT_EQ(pointCloud.points.size(), 4);
    
    // Check that all points are valid
    for (const auto& point : pointCloud.points) {
        EXPECT_TRUE(point.valid);
        EXPECT_GT(point.z, 0.0f); // All should have positive world Z
    }
}

TEST_F(CoordinateTransformTest, TransformFrameWithInvalidPixels) {
    CoordinateTransform transform;
    
    // Load calibration
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = 0.0f;
    
    transform.loadFromCalibration(calibProfile);
    
    // Create test frame with mix of valid and invalid pixels
    DepthFrame depthFrame;
    depthFrame.width = 2;
    depthFrame.height = 2;
    depthFrame.data = {1000.0f, 0.0f, -100.0f, 1200.0f}; // Mixed valid/invalid
    depthFrame.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    InternalPointCloud pointCloud;
    
    bool result = transform.transformFrameToWorld(depthFrame, pointCloud);
    
    EXPECT_TRUE(result); // Should succeed since some pixels are valid
    EXPECT_EQ(pointCloud.width, 2);
    EXPECT_EQ(pointCloud.height, 2);
    EXPECT_EQ(pointCloud.points.size(), 4);
    
    // Check validity pattern
    EXPECT_TRUE(pointCloud.points[0].valid);   // First pixel valid
    EXPECT_FALSE(pointCloud.points[1].valid);  // Second pixel invalid (zero depth)
    EXPECT_FALSE(pointCloud.points[2].valid);  // Third pixel invalid (negative depth)
    EXPECT_TRUE(pointCloud.points[3].valid);   // Fourth pixel valid
}

TEST_F(CoordinateTransformTest, TransformFrameAllInvalidPixels) {
    CoordinateTransform transform;
    
    // Load calibration
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = 0.0f;
    
    transform.loadFromCalibration(calibProfile);
    
    // Create test frame with all invalid pixels
    DepthFrame depthFrame;
    depthFrame.width = 2;
    depthFrame.height = 2;
    depthFrame.data = {0.0f, -100.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    depthFrame.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    InternalPointCloud pointCloud;
    
    bool result = transform.transformFrameToWorld(depthFrame, pointCloud);
    
    EXPECT_FALSE(result); // Should fail since no pixels are valid
    
    // Check that all points are marked invalid
    for (const auto& point : pointCloud.points) {
        EXPECT_FALSE(point.valid);
    }
}

TEST_F(CoordinateTransformTest, PlaneBasedValidationAcceptsValidPoints) {
    CoordinateTransform transform;
    
    // Set up calibration with plane-based validation
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -0.5f;  // Base plane at z = 0.5m
    
    transform.loadFromCalibration(calibProfile);
    
    // Test point with realistic Kinect depth values (900mm = typical range)
    Point3D result = transform.transformPixelToWorld(320, 240, 900.0f); // 0.9m depth - realistic Kinect range
    
    EXPECT_TRUE(result.valid) << "Point at realistic depth should be accepted by plane validation";
    EXPECT_GT(result.z, 0.0f);
}

TEST_F(CoordinateTransformTest, PlaneBasedValidationRejectsTooClose) {
    CoordinateTransform transform;
    
    // Set up calibration with plane-based validation
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -0.5f;  // Base plane at z = 0.5m
    
    transform.loadFromCalibration(calibProfile);
    
    // Test point that's too close to camera - results in z values above validation limit 
    Point3D result = transform.transformPixelToWorld(320, 240, 600.0f); // 0.6m depth - should exceed upper validation bound
    
    EXPECT_FALSE(result.valid) << "Point too close to camera should be rejected by plane validation";
    EXPECT_EQ(result.x, 0.0f);
    EXPECT_EQ(result.y, 0.0f);
    EXPECT_EQ(result.z, 0.0f);
}

TEST_F(CoordinateTransformTest, PlaneBasedValidationRejectsTooFar) {
    CoordinateTransform transform;
    
    // Set up calibration with plane-based validation  
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -0.5f;  // Base plane at z = 0.5m
    
    transform.loadFromCalibration(calibProfile);
    
    // Test point that's too far from camera - results in very high z values (should be rejected)
    Point3D result = transform.transformPixelToWorld(320, 240, 1500.0f); // 1.5m depth - too far, creates very high z
    
    EXPECT_FALSE(result.valid) << "Point too far from camera should be rejected by plane validation";
    EXPECT_EQ(result.x, 0.0f);
    EXPECT_EQ(result.y, 0.0f);
    EXPECT_EQ(result.z, 0.0f);
}

TEST_F(CoordinateTransformTest, PlaneBasedValidationWithAngledPlane) {
    CoordinateTransform transform;
    
    // Set up calibration with angled base plane
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.a = 0.1f;   // Slight angle
    calibProfile.basePlaneCalibration.basePlane.b = 0.0f;
    calibProfile.basePlaneCalibration.basePlane.c = 1.0f;
    calibProfile.basePlaneCalibration.basePlane.d = -0.5f;
    
    transform.loadFromCalibration(calibProfile);
    
    // Test points at different x positions (angled plane affects validation differently)
    Point3D center = transform.transformPixelToWorld(320, 240, 900.0f);
    Point3D leftEdge = transform.transformPixelToWorld(100, 240, 900.0f);
    Point3D rightEdge = transform.transformPixelToWorld(540, 240, 900.0f);
    
    // Due to angled plane, validation results may differ based on x position
    // At minimum, center should be valid for reasonable depth
    EXPECT_TRUE(center.valid) << "Center point should be valid with angled plane";
}

// Integration test - only runs if calibration file exists
TEST_F(CoordinateTransformTest, DISABLED_LoadRealCalibrationProfile) {
    CoordinateTransform transform;
    
    // This would test loading actual calibration file
    // Disabled by default since it requires real calibration data
    SensorCalibration calibLoader;
    SensorCalibrationProfile calibProfile;
    
    bool loaded = calibLoader.loadCalibrationProfile("kinect-v1", calibProfile);
    if (loaded) {
        bool transformed = transform.loadFromCalibration(calibProfile);
        EXPECT_TRUE(transformed);
        EXPECT_TRUE(transform.isReady());
    }
}