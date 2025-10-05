#include "DepthCorrector.h"
#include "tools/calibration/SensorCalibration.h"
#include <filesystem>
#include <cmath>
#include <algorithm>

using caldera::backend::common::Logger;
using caldera::backend::common::DepthFrame;

namespace caldera::backend::processing {

// For convenience within this namespace
using CorrectionProfile = caldera::backend::processing::CorrectionProfile;

DepthCorrector::DepthCorrector()
    : logger_(common::Logger::instance().get("DepthCorrector")) {
}

bool DepthCorrector::loadProfile(const std::string& sensorId) {
    logger_->debug("Loading depth correction profile for sensor: {}", sensorId);
    
    // Load calibration data first
    tools::calibration::SensorCalibration calibrator;
    tools::calibration::SensorCalibrationProfile calibProfile;
    
    if (!calibrator.loadCalibrationProfile(sensorId, calibProfile)) {
        logger_->error("Failed to load calibration profile for sensor: {}", sensorId);
        return false;
    }
    
    // Create correction profile from calibration data
    profile_ = createProfile(sensorId, calibProfile);
    
    if (!profile_.isValid) {
        logger_->error("Failed to create depth correction profile for sensor: {}", sensorId);
        return false;
    }
    
    logger_->info("Loaded depth correction profile for sensor {} ({}x{})", 
                  sensorId, profile_.width, profile_.height);
    return true;
}

float DepthCorrector::correctPixel(int x, int y, float rawDepth) const {
    if (!isValidPixel(x, y)) {
        return rawDepth;  // Return uncorrected if out of bounds
    }
    
    // For now, simple per-pixel multiplicative correction
    // Future: add radial distortion, intrinsic matrix transforms
    float correctionFactor = getCorrectionFactor(x, y);
    return rawDepth * correctionFactor;
}

void DepthCorrector::correctFrame(common::RawDepthFrame& frame) const {
    if (!profile_.isValid) {
        logger_->warn("No correction profile loaded, skipping frame correction");
        return;
    }
    
    if (frame.width != profile_.width || frame.height != profile_.height) {
        logger_->warn("Frame size mismatch: {}x{} vs profile {}x{}", 
                     frame.width, frame.height, profile_.width, profile_.height);
        return;
    }
    
    // Apply correction to each pixel
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            int idx = y * frame.width + x;
            if (idx < static_cast<int>(frame.data.size())) {
                float rawDepth = static_cast<float>(frame.data[idx]);
                
                // Skip invalid/zero depths
                if (rawDepth > 0.0f) {
                    float corrected = correctPixel(x, y, rawDepth);
                    frame.data[idx] = static_cast<uint16_t>(std::round(corrected));
                }
            }
        }
    }
}

CorrectionProfile DepthCorrector::createProfile(
    const std::string& sensorId,
    const tools::calibration::SensorCalibrationProfile& calibrationProfile) {
    
    CorrectionProfile profile;
    profile.sensorId = sensorId;
    
    // Determine frame dimensions based on sensor type
    if (calibrationProfile.sensorType == "kinect-v1") {
        profile.width = 640;
        profile.height = 480;
    } else if (calibrationProfile.sensorType == "kinect-v2") {
        profile.width = 512;  // Kinect v2 depth resolution
        profile.height = 424;
    } else {
        // Unknown sensor type
        return profile;  // isValid remains false
    }
    
    // Initialize correction factors
    int totalPixels = profile.width * profile.height;
    profile.pixelCorrections.resize(totalPixels, 1.0f);  // Default: no correction
    
    // For now, create uniform correction (no per-pixel variation)
    // Future: load actual per-pixel corrections from calibration data
    // This would come from measuring flat surface at different distances
    
    // Apply basic depth-dependent correction based on base plane
    const auto& plane = calibrationProfile.basePlaneCalibration.basePlane;
    float baseDistance = -plane.d;  // Distance to base plane
    
    // Simple distance-based correction factor
    // Closer pixels might need slight correction due to lens distortion
    for (int y = 0; y < profile.height; ++y) {
        for (int x = 0; x < profile.width; ++x) {
            int idx = y * profile.width + x;
            
            // Distance from center (normalized)
            float centerX = profile.width * 0.5f;
            float centerY = profile.height * 0.5f;
            float dx = (x - centerX) / centerX;
            float dy = (y - centerY) / centerY;
            float distanceFromCenter = std::sqrt(dx*dx + dy*dy);
            
            // Apply minimal radial correction (placeholder)
            // Real correction would come from calibration measurements
            float radialCorrection = 1.0f + distanceFromCenter * 0.002f;  // Very small effect
            
            profile.pixelCorrections[idx] = radialCorrection;
        }
    }
    
    profile.isValid = true;
    return profile;
}

float DepthCorrector::getCorrectionFactor(int x, int y) const {
    if (!isValidPixel(x, y)) {
        return 1.0f;
    }
    
    int idx = y * profile_.width + x;
    return profile_.pixelCorrections[idx];
}

bool DepthCorrector::isValidPixel(int x, int y) const {
    return x >= 0 && x < profile_.width && y >= 0 && y < profile_.height;
}

} // namespace caldera::backend::processing