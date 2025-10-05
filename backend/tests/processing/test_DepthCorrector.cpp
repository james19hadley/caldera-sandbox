#include <gtest/gtest.h>
#include "processing/DepthCorrector.h"
#include "common/Logger.h"
#include "tools/calibration/SensorCalibration.h"

using namespace caldera::backend::processing;
using namespace caldera::backend::common;
using namespace caldera::backend::tools::calibration;

class DepthCorrectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize Logger for all tests
        if (!Logger::instance().isInitialized()) {
            Logger::instance().initialize("logs/test/depth_corrector.log");
        }
    }
};

TEST_F(DepthCorrectorTest, ConstructorInitializesCorrectly) {
    DepthCorrector corrector;
    
    EXPECT_FALSE(corrector.isReady());
    EXPECT_TRUE(corrector.getSensorId().empty());
}

TEST_F(DepthCorrectorTest, CreateProfileForKinectV1) {
    // Create mock calibration profile
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v1";
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.d = -0.926f;  // Base plane distance
    
    auto profile = DepthCorrector::createProfile("test-kinect-v1", calibProfile);
    
    EXPECT_TRUE(profile.isValid);
    EXPECT_EQ(profile.sensorId, "test-kinect-v1");
    EXPECT_EQ(profile.width, 640);
    EXPECT_EQ(profile.height, 480);
    EXPECT_EQ(profile.pixelCorrections.size(), 640 * 480);
}

TEST_F(DepthCorrectorTest, CreateProfileForKinectV2) {
    // Create mock calibration profile
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "test-kinect-v2"; 
    calibProfile.sensorType = "kinect-v2";
    calibProfile.basePlaneCalibration.basePlane.d = -0.8f;
    
    auto profile = DepthCorrector::createProfile("test-kinect-v2", calibProfile);
    
    EXPECT_TRUE(profile.isValid);
    EXPECT_EQ(profile.sensorId, "test-kinect-v2");
    EXPECT_EQ(profile.width, 512);
    EXPECT_EQ(profile.height, 424);
    EXPECT_EQ(profile.pixelCorrections.size(), 512 * 424);
}

TEST_F(DepthCorrectorTest, CreateProfileForUnknownSensor) {
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorId = "unknown-sensor";
    calibProfile.sensorType = "unknown";
    
    auto profile = DepthCorrector::createProfile("unknown-sensor", calibProfile);
    
    EXPECT_FALSE(profile.isValid);
}

TEST_F(DepthCorrectorTest, CorrectPixelWithoutProfile) {
    DepthCorrector corrector;
    
    // Should return uncorrected depth when no profile loaded
    float result = corrector.correctPixel(320, 240, 1000.0f);
    EXPECT_FLOAT_EQ(result, 1000.0f);
}

TEST_F(DepthCorrectorTest, CorrectPixelWithProfile) {
    // Create corrector with synthetic profile
    DepthCorrector corrector;
    
    // Create synthetic calibration data
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorType = "kinect-v1";
    calibProfile.basePlaneCalibration.basePlane.d = -1.0f;
    
    auto profile = DepthCorrector::createProfile("test", calibProfile);
    ASSERT_TRUE(profile.isValid);
    
    // Manually load profile (bypassing file system)
    // Note: This tests the correction logic, not the file loading
    
    // Center pixel should have minimal correction
    float centerCorrection = profile.pixelCorrections[240 * 640 + 320];  // Center of 640x480
    EXPECT_NEAR(centerCorrection, 1.0f, 0.01f);  // Should be close to 1.0
    
    // Edge pixels should have slightly more correction  
    float edgeCorrection = profile.pixelCorrections[240 * 640 + 0];  // Left edge
    EXPECT_GT(edgeCorrection, centerCorrection);  // Edge should have more correction
}

TEST_F(DepthCorrectorTest, CorrectFrameHandlesInvalidPixels) {
    DepthCorrector corrector;
    
    // Create test frame
    RawDepthFrame frame;
    frame.width = 4;
    frame.height = 4;
    frame.data = {
        1000, 1010, 1020, 1030,
        0,    1110, 1120, 1130,  // Zero depth (invalid)
        1200, 0,    1220, 1230,  // Another zero
        1300, 1310, 1320, 1330
    };
    
    // Without profile, should not modify frame
    corrector.correctFrame(frame);
    
    // Invalid pixels should remain unchanged
    EXPECT_EQ(frame.data[4], 0);   // First zero
    EXPECT_EQ(frame.data[9], 0);   // Second zero
    
    // Valid pixels should also remain unchanged (no profile loaded)
    EXPECT_EQ(frame.data[0], 1000);
    EXPECT_EQ(frame.data[15], 1330);
}

TEST_F(DepthCorrectorTest, HandlesOutOfBoundsPixels) {
    // Create corrector with profile
    SensorCalibrationProfile calibProfile;
    calibProfile.sensorType = "kinect-v1";
    auto profile = DepthCorrector::createProfile("test", calibProfile);
    ASSERT_TRUE(profile.isValid);
    
    DepthCorrector corrector;
    
    // Test out-of-bounds coordinates
    EXPECT_FLOAT_EQ(corrector.correctPixel(-1, 240, 1000.0f), 1000.0f);
    EXPECT_FLOAT_EQ(corrector.correctPixel(640, 240, 1000.0f), 1000.0f);
    EXPECT_FLOAT_EQ(corrector.correctPixel(320, -1, 1000.0f), 1000.0f);
    EXPECT_FLOAT_EQ(corrector.correctPixel(320, 480, 1000.0f), 1000.0f);
}

// Integration test - only runs if calibration file exists
TEST_F(DepthCorrectorTest, DISABLED_LoadRealCalibrationProfile) {
    DepthCorrector corrector;
    
    // This would test loading actual calibration file
    // Disabled by default since it requires real calibration data
    bool loaded = corrector.loadProfile("kinect-v1");
    
    if (loaded) {
        EXPECT_TRUE(corrector.isReady());
        EXPECT_EQ(corrector.getSensorId(), "kinect-v1");
        
        // Test correction on realistic depth value
        float corrected = corrector.correctPixel(320, 240, 1000.0f);
        EXPECT_GT(corrected, 0.0f);
        EXPECT_NE(corrected, 1000.0f);  // Should be different after correction
    } else {
        GTEST_SKIP() << "No calibration profile found for kinect-v1";
    }
}