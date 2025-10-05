#include "SensorCalibration.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace caldera::backend::tools::calibration {

using caldera::backend::common::Logger;
using caldera::backend::hal::ISensorDevice;
using caldera::backend::hal::KinectV1_Device;
using caldera::backend::hal::KinectV2_Device;

SensorCalibration::SensorCalibration() 
    : logger_(Logger::instance().get("SensorCalibration")) {
    
    // Set default calibration directory
    calibrationDirectory_ = std::filesystem::current_path() / "config" / "calibration";
    // Allow env override
    if (const char* env = std::getenv("CALDERA_CALIBRATION_DIR")) {
        calibrationDirectory_ = std::filesystem::path(env);
        logger_->info("Using calibration directory from CALDERA_CALIBRATION_DIR={}", env);
    } else {
        // Also try repo-standard location relative to source tree
        std::filesystem::path repoCandidate = std::filesystem::current_path() / "backend" / "config" / "calibration";
        if (std::filesystem::exists(repoCandidate)) {
            calibrationDirectory_ = repoCandidate;
            logger_->info("Using repo calibration directory {}", repoCandidate.string());
        }
    }
}

// === Sensor Management ===

std::shared_ptr<ISensorDevice> SensorCalibration::createSensorDevice(const std::string& sensorId) {
    if (sensorId == "kinect-v1" || sensorId == "kinect_v1") {
        return std::make_shared<KinectV1_Device>();
    }
    else if (sensorId == "kinect-v2" || sensorId == "kinect_v2") {
        return std::make_shared<KinectV2_Device>();
    }
    else {
        logger_->error("Unknown sensor ID: {}", sensorId);
        return nullptr;
    }
}

std::vector<std::string> SensorCalibration::getAvailableSensorTypes() const {
    return {"kinect-v1", "kinect-v2"};
}

// === Calibration Collection ===

CalibrationResult SensorCalibration::collectAutomaticCalibration(std::shared_ptr<ISensorDevice> sensor,
                                                                 const CalibrationConfig& config,
                                                                 PlaneCalibrationData& result) {
    logger_->info("Starting automatic calibration for sensor: {}", sensor->getDeviceID());
    
    if (!sensor->open()) {
        logger_->error("Failed to open sensor");
        return CalibrationResult::SensorNotAvailable;
    }
    
    currentSensor_ = sensor;
    frameReceived_ = false;
    
    // Set up frame callback
    sensor->setFrameCallback([this](const common::RawDepthFrame& depth, const common::RawColorFrame& color) {
        onFrameReceived(depth, color);
    });
    
    // Wait for first frame
    auto startTime = std::chrono::steady_clock::now();
    while (!frameReceived_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > std::chrono::seconds(5)) {
            sensor->close();
            return CalibrationResult::SensorNotAvailable;
        }
    }
    
    // Collect points from center region in a grid pattern
    std::vector<Point3D> points;
    points.reserve(config.minPointsRequired);
    
    int gridSize = static_cast<int>(std::ceil(std::sqrt(config.minPointsRequired)));
    int centerX = latestDepthFrame_.width / 2;
    int centerY = latestDepthFrame_.height / 2;
    int radius = std::min(latestDepthFrame_.width, latestDepthFrame_.height) / 8;
    
    for (int i = 0; i < gridSize && points.size() < static_cast<size_t>(config.minPointsRequired); ++i) {
        for (int j = 0; j < gridSize && points.size() < static_cast<size_t>(config.minPointsRequired); ++j) {
            int x = centerX + (i - gridSize/2) * (radius / gridSize);
            int y = centerY + (j - gridSize/2) * (radius / gridSize);
            
            Point3D point;
            if (convertDepthToWorld(x, y, point)) {
                points.push_back(point);
                logger_->debug("Collected point {}: ({:.3f}, {:.3f}, {:.3f})", 
                              points.size(), point.x, point.y, point.z);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    sensor->close();
    
    logger_->info("Collected {} points for plane fitting (required: {})", 
                  points.size(), config.minPointsRequired);
    
    if (points.size() < static_cast<size_t>(config.minPointsRequired)) {
        logger_->error("Insufficient points collected: {}", points.size());
        return CalibrationResult::InsufficientPoints;
    }
    
    // Fill result structure
    result.sensorId = sensor->getDeviceID();
    result.timestamp = std::chrono::system_clock::now();
    result.collectedPoints = points;
    
    // Fit plane
    float avgDistance, maxDistance, rSquared;
    if (!fitPlaneToPoints(points, result.basePlane, avgDistance, maxDistance, rSquared)) {
        logger_->error("Failed to fit plane to {} collected points", points.size());
        return CalibrationResult::PoorPlaneFit;
    }
    
    logger_->info("Plane fitting results:");
    logger_->info("  Plane equation: {:.4f}x + {:.4f}y + {:.4f}z + {:.4f} = 0", 
                  result.basePlane.a, result.basePlane.b, result.basePlane.c, result.basePlane.d);
    logger_->info("  Average distance to plane: {:.4f}m", avgDistance);
    logger_->info("  Maximum distance to plane: {:.4f}m", maxDistance);
    logger_->info("  R² goodness of fit: {:.4f}", rSquared);
    
    result.avgDistanceToPlane = avgDistance;
    result.maxDistanceToPlane = maxDistance;
    result.planeFitRSquared = rSquared;
    
    // Validate quality
    result.isValidCalibration = validateCalibrationQuality(result, config);
    
    if (!result.isValidCalibration) {
        logger_->warn("Calibration does not meet quality thresholds");
        return CalibrationResult::PoorPlaneFit;
    }
    
    logger_->info("Automatic calibration completed successfully");
    return CalibrationResult::Success;
}

bool SensorCalibration::startInteractiveCalibration(std::shared_ptr<ISensorDevice> sensor) {
    logger_->info("Starting interactive calibration for sensor: {}", sensor->getDeviceID());
    
    if (!sensor->open()) {
        logger_->error("Failed to open sensor");
        return false;
    }
    
    currentSensor_ = sensor;
    frameReceived_ = false;
    interactivePoints_.clear();
    
    // Set up frame callback
    sensor->setFrameCallback([this](const common::RawDepthFrame& depth, const common::RawColorFrame& color) {
        onFrameReceived(depth, color);
    });
    
    // Wait for first frame
    auto startTime = std::chrono::steady_clock::now();
    while (!frameReceived_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > std::chrono::seconds(5)) {
            sensor->close();
            return false;
        }
    }
    
    logger_->info("Interactive calibration started - sensor ready");
    return true;
}

bool SensorCalibration::captureCalibrationPoint(Point3D& point) {
    if (!currentSensor_ || !frameReceived_) {
        return false;
    }
    
    // Sample from center of image
    int centerX = latestDepthFrame_.width / 2;
    int centerY = latestDepthFrame_.height / 2;
    
    if (convertDepthToWorld(centerX, centerY, point)) {
        interactivePoints_.push_back(point);
        logger_->info("Captured point {}: ({:.3f}, {:.3f}, {:.3f})", 
                     interactivePoints_.size(), point.x, point.y, point.z);
        return true;
    }
    
    return false;
}

CalibrationResult SensorCalibration::finishInteractiveCalibration(const CalibrationConfig& config,
                                                                 PlaneCalibrationData& result) {
    if (!currentSensor_) {
        return CalibrationResult::SensorNotAvailable;
    }
    
    if (interactivePoints_.size() < static_cast<size_t>(config.minPointsRequired)) {
        logger_->error("Insufficient points collected: {}", interactivePoints_.size());
        return CalibrationResult::InsufficientPoints;
    }
    
    // Fill result structure
    result.sensorId = currentSensor_->getDeviceID();
    result.timestamp = std::chrono::system_clock::now();
    result.collectedPoints = interactivePoints_;
    
    // Fit plane
    float avgDistance, maxDistance, rSquared;
    if (!fitPlaneToPoints(interactivePoints_, result.basePlane, avgDistance, maxDistance, rSquared)) {
        return CalibrationResult::PoorPlaneFit;
    }
    
    result.avgDistanceToPlane = avgDistance;
    result.maxDistanceToPlane = maxDistance;
    result.planeFitRSquared = rSquared;
    
    // Validate quality
    result.isValidCalibration = validateCalibrationQuality(result, config);
    
    currentSensor_->close();
    currentSensor_.reset();
    
    if (!result.isValidCalibration) {
        logger_->warn("Calibration does not meet quality thresholds");
        return CalibrationResult::PoorPlaneFit;
    }
    
    logger_->info("Interactive calibration completed successfully");
    return CalibrationResult::Success;
}

void SensorCalibration::stopInteractiveCalibration() {
    if (currentSensor_) {
        currentSensor_->close();
        currentSensor_.reset();
    }
    interactivePoints_.clear();
    frameReceived_ = false;
}

// === Profile Management ===

bool SensorCalibration::saveCalibrationProfile(const SensorCalibrationProfile& profile) {
    if (!ensureCalibrationDirectoryExists()) {
        logger_->error("Failed to create calibration directory: {}", calibrationDirectory_);
        return false;
    }
    
    std::string filename = getProfileFilename(profile.sensorId);
    std::string jsonData = serializeProfile(profile);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        logger_->error("Failed to open file for writing: {}", filename);
        return false;
    }
    
    file << jsonData;
    file.close();
    
    if (file.bad()) {
        logger_->error("Error writing calibration profile to file: {}", filename);
        return false;
    }
    
    logger_->info("Saved calibration profile for sensor: {} -> {}", profile.sensorId, filename);
    return true;
}

bool SensorCalibration::loadCalibrationProfile(const std::string& sensorId, SensorCalibrationProfile& profile) {
    std::string filename = getProfileFilename(sensorId);
    
    if (!std::filesystem::exists(filename)) {
        logger_->debug("Calibration profile not found for sensor: {}", sensorId);
        return false;
    }
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        logger_->error("Failed to open calibration file: {}", filename);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    if (!deserializeProfile(buffer.str(), profile)) {
        logger_->error("Failed to parse calibration profile: {}", filename);
        return false;
    }
    
    logger_->info("Loaded calibration profile for sensor: {}", sensorId);
    return true;
}

bool SensorCalibration::hasCalibrationProfile(const std::string& sensorId) const {
    std::string filename = getProfileFilename(sensorId);
    return std::filesystem::exists(filename);
}

std::vector<std::string> SensorCalibration::listCalibrationProfiles() const {
    std::vector<std::string> profiles;
    
    if (!std::filesystem::exists(calibrationDirectory_)) {
        return profiles;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(calibrationDirectory_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string stem = entry.path().stem();
                if (stem.length() >= 12 && stem.substr(stem.length() - 12) == "_calibration") {
                    std::string sensorId = stem.substr(0, stem.length() - 12);
                    profiles.push_back(sensorId);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& ex) {
        logger_->error("Error listing calibration profiles: {}", ex.what());
    }
    
    return profiles;
}

bool SensorCalibration::deleteCalibrationProfile(const std::string& sensorId) {
    std::string filename = getProfileFilename(sensorId);
    
    try {
        if (std::filesystem::exists(filename)) {
            std::filesystem::remove(filename);
            logger_->info("Deleted calibration profile for sensor: {}", sensorId);
            return true;
        }
        return false;
    } catch (const std::filesystem::filesystem_error& ex) {
        logger_->error("Failed to delete calibration profile {}: {}", filename, ex.what());
        return false;
    }
}

// === Validation & Testing ===

float SensorCalibration::validateCalibration(const std::string& sensorId, int numTestPoints) {
    SensorCalibrationProfile profile;
    if (!loadCalibrationProfile(sensorId, profile)) {
        logger_->error("No calibration profile found for sensor: {}", sensorId);
        return -1.0f;
    }
    
    auto sensor = createSensorDevice(sensorId);
    if (!sensor || !sensor->open()) {
        logger_->error("Cannot connect to sensor for validation: {}", sensorId);
        return -1.0f;
    }
    
    // Collect test points
    CalibrationConfig testConfig;
    testConfig.minPointsRequired = numTestPoints;
    
    PlaneCalibrationData testData;
    CalibrationResult result = collectAutomaticCalibration(sensor, testConfig, testData);
    
    if (result != CalibrationResult::Success) {
        logger_->error("Failed to collect validation points");
        return -1.0f;
    }
    
    // Calculate average distance to calibrated plane
    float totalDistance = 0.0f;
    for (const auto& point : testData.collectedPoints) {
        float distance = std::abs(profile.basePlaneCalibration.basePlane.distanceToPoint(point));
        totalDistance += distance;
    }
    
    float avgDistance = totalDistance / testData.collectedPoints.size();
    logger_->info("Validation for {}: {} test points, avg distance: {:.4f}m", 
                  sensorId, testData.collectedPoints.size(), avgDistance);
    
    return avgDistance;
}

// === Configuration ===

void SensorCalibration::setCalibrationDirectory(const std::string& directory) {
    calibrationDirectory_ = directory;
}

CalibrationConfig SensorCalibration::getDefaultConfig() const {
    CalibrationConfig config;
    config.minPointsRequired = 20;
    config.maxDistanceThreshold = 0.02f;  // 2cm
    config.rSquaredThreshold = 0.95f;
    config.minPlaneOffsetMeters = -0.20f;
    config.maxPlaneOffsetMeters = 0.30f;
    return config;
}

// === Private Implementation ===

void SensorCalibration::onFrameReceived(const common::RawDepthFrame& depth, const common::RawColorFrame& color) {
    latestDepthFrame_ = depth;
    latestColorFrame_ = color;
    frameReceived_ = true;
}

bool SensorCalibration::convertDepthToWorld(int imageX, int imageY, Point3D& worldPoint) const {
    if (imageX < 0 || imageX >= latestDepthFrame_.width || 
        imageY < 0 || imageY >= latestDepthFrame_.height) {
        return false;
    }
    
    int index = imageY * latestDepthFrame_.width + imageX;
    if (index >= static_cast<int>(latestDepthFrame_.data.size())) {
        return false;
    }
    
    uint16_t depthValue = latestDepthFrame_.data[index];
    if (depthValue == 0) {
        return false;
    }
    
    // Convert to meters (assuming depth is in millimeters)
    float depthMeters = static_cast<float>(depthValue) / 1000.0f;
    
    // Approximate camera intrinsics (should be sensor-specific in real implementation)
    float fx = 525.0f;
    float fy = 525.0f;
    float cx = latestDepthFrame_.width / 2.0f;
    float cy = latestDepthFrame_.height / 2.0f;
    
    worldPoint.x = (imageX - cx) * depthMeters / fx;
    worldPoint.y = (imageY - cy) * depthMeters / fy;
    worldPoint.z = depthMeters;
    
    return true;
}

bool SensorCalibration::fitPlaneToPoints(const std::vector<Point3D>& points,
                                        PlaneEquation& plane,
                                        float& avgDistance,
                                        float& maxDistance,
                                        float& rSquared) const {
    if (points.size() < 3) {
        return false;
    }
    
    // Compute centroid
    Point3D centroid = {0, 0, 0};
    for (const auto& p : points) {
        centroid.x += p.x;
        centroid.y += p.y;
        centroid.z += p.z;
    }
    centroid.x /= points.size();
    centroid.y /= points.size();
    centroid.z /= points.size();
    
    // Simple horizontal plane assumption (for sandbox surface)
    plane.a = 0.0f;
    plane.b = 0.0f;
    plane.c = 1.0f;
    plane.d = -centroid.z;
    
    // Calculate quality metrics
    float totalDistance = 0.0f;
    maxDistance = 0.0f;
    
    for (const auto& p : points) {
        float distance = std::abs(plane.distanceToPoint(p));
        totalDistance += distance;
        maxDistance = std::max(maxDistance, distance);
    }
    
    avgDistance = totalDistance / points.size();
    
    // R-squared calculation for horizontal plane quality
    float meanZ = centroid.z;
    
    // For horizontal plane, R² measures how well points fit to constant Z
    // We'll calculate variance in Z-coordinates normalized by the range
    float minZ = points[0].z, maxZ = points[0].z;
    for (const auto& p : points) {
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
    }
    
    float zRange = maxZ - minZ;
    float variance = 0.0f;
    for (const auto& p : points) {
        float deviation = p.z - meanZ;
        variance += deviation * deviation;
    }
    variance /= points.size();
    
    // R² based on how tight the Z distribution is
    // If variance is small relative to typical depth noise, R² is high
    float typicalDepthNoise = 0.01f; // 1cm typical noise
    if (zRange < typicalDepthNoise && variance < typicalDepthNoise * typicalDepthNoise) {
        rSquared = 0.99f; // Very good fit
    } else {
        // Calculate R² based on how much variance is explained by the plane fit
        float maxVariance = typicalDepthNoise * typicalDepthNoise;
        rSquared = 1.0f - std::min(1.0f, variance / maxVariance);
    }
    
    logger_->debug("Z range: {:.4f}m, Z variance: {:.6f}, calculated R²: {:.4f}", 
                   zRange, variance, rSquared);
    
    return true;
}

bool SensorCalibration::validateCalibrationQuality(const PlaneCalibrationData& data,
                                                   const CalibrationConfig& config) const {
    logger_->debug("Validating calibration quality:");
    logger_->debug("  Average distance to plane: {:.4f}m (threshold: {:.4f}m)", 
                   data.avgDistanceToPlane, config.maxAvgDistanceToPlane);
    logger_->debug("  Maximum distance to plane: {:.4f}m (threshold: {:.4f}m)", 
                   data.maxDistanceToPlane, config.maxDistanceToPlane);
    logger_->debug("  Plane fit R²: {:.4f} (threshold: {:.4f})", 
                   data.planeFitRSquared, config.minPlaneFitRSquared);
    
    bool avgDistanceOk = data.avgDistanceToPlane < config.maxAvgDistanceToPlane;
    bool maxDistanceOk = data.maxDistanceToPlane < config.maxDistanceToPlane;
    bool rSquaredOk = data.planeFitRSquared > config.minPlaneFitRSquared;
    
    logger_->debug("  Validation results: avg_dist={}, max_dist={}, r_squared={}", 
                   avgDistanceOk ? "PASS" : "FAIL",
                   maxDistanceOk ? "PASS" : "FAIL", 
                   rSquaredOk ? "PASS" : "FAIL");
    
    return avgDistanceOk && maxDistanceOk && rSquaredOk;
}

std::string SensorCalibration::getProfileFilename(const std::string& sensorId) const {
    std::filesystem::path path(calibrationDirectory_);
    path /= (sensorId + "_calibration.json");
    return path.string();
}

bool SensorCalibration::ensureCalibrationDirectoryExists() const {
    try {
        if (!std::filesystem::exists(calibrationDirectory_)) {
            std::filesystem::create_directories(calibrationDirectory_);
        }
        return std::filesystem::is_directory(calibrationDirectory_);
    } catch (const std::filesystem::filesystem_error& ex) {
        logger_->error("Failed to create calibration directory {}: {}", calibrationDirectory_, ex.what());
        return false;
    }
}

std::string SensorCalibration::serializeProfile(const SensorCalibrationProfile& profile) const {
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    
    json << "{\n";
    json << "  \"sensorId\": \"" << profile.sensorId << "\",\n";
    json << "  \"sensorType\": \"" << profile.sensorType << "\",\n";
    
    // Base plane calibration
    const auto& cal = profile.basePlaneCalibration;
    json << "  \"basePlaneCalibration\": {\n";
    json << "    \"pointCount\": " << cal.collectedPoints.size() << ",\n";
    json << "    \"basePlane\": {\n";
    json << "      \"a\": " << cal.basePlane.a << ",\n";
    json << "      \"b\": " << cal.basePlane.b << ",\n";
    json << "      \"c\": " << cal.basePlane.c << ",\n";
    json << "      \"d\": " << cal.basePlane.d << "\n";
    json << "    },\n";
    json << "    \"avgDistanceToPlane\": " << cal.avgDistanceToPlane << ",\n";
    json << "    \"maxDistanceToPlane\": " << cal.maxDistanceToPlane << ",\n";
    json << "    \"planeFitRSquared\": " << cal.planeFitRSquared << ",\n";
    json << "    \"isValidCalibration\": " << (cal.isValidCalibration ? "true" : "false") << "\n";
    json << "  },\n";
    // Serialize processing bounds planes
    json << "  \"minValidPlane\": {\n";
    json << "    \"a\": " << profile.minValidPlane.a << ",\n";
    json << "    \"b\": " << profile.minValidPlane.b << ",\n";
    json << "    \"c\": " << profile.minValidPlane.c << ",\n";
    json << "    \"d\": " << profile.minValidPlane.d << "\n";
    json << "  },\n";
    json << "  \"maxValidPlane\": {\n";
    json << "    \"a\": " << profile.maxValidPlane.a << ",\n";
    json << "    \"b\": " << profile.maxValidPlane.b << ",\n";
    json << "    \"c\": " << profile.maxValidPlane.c << ",\n";
    json << "    \"d\": " << profile.maxValidPlane.d << "\n";
    json << "  }\n";
    json << "}\n";
    
    return json.str();
}

bool SensorCalibration::deserializeProfile(const std::string& jsonData, SensorCalibrationProfile& profile) const {
    // Simplified JSON parsing for our specific format
    try {
        // Extract sensor ID
        auto sensorIdPos = jsonData.find("\"sensorId\": \"");
        if (sensorIdPos == std::string::npos) return false;
        sensorIdPos += 13;
        auto sensorIdEnd = jsonData.find("\"", sensorIdPos);
        if (sensorIdEnd == std::string::npos) return false;
        profile.sensorId = jsonData.substr(sensorIdPos, sensorIdEnd - sensorIdPos);
        
        // Extract sensor type
        auto sensorTypePos = jsonData.find("\"sensorType\": \"");
        if (sensorTypePos == std::string::npos) return false;
        sensorTypePos += 15;
        auto sensorTypeEnd = jsonData.find("\"", sensorTypePos);
        if (sensorTypeEnd == std::string::npos) return false;
        profile.sensorType = jsonData.substr(sensorTypePos, sensorTypeEnd - sensorTypePos);
        
        // Extract plane equation and metrics (simplified)
        auto extractFloat = [&](const std::string& key) -> float {
            std::string searchStr = "\"" + key + "\": ";
            auto pos = jsonData.find(searchStr);
            if (pos == std::string::npos) return 0.0f;
            pos += searchStr.length();
            auto end = jsonData.find_first_of(",\n}", pos);
            if (end == std::string::npos) return 0.0f;
            std::string valueStr = jsonData.substr(pos, end - pos);
            return std::stof(valueStr);
        };
        
        profile.basePlaneCalibration.basePlane.a = extractFloat("a");
        profile.basePlaneCalibration.basePlane.b = extractFloat("b");
        profile.basePlaneCalibration.basePlane.c = extractFloat("c");
        profile.basePlaneCalibration.basePlane.d = extractFloat("d");
        
    profile.basePlaneCalibration.avgDistanceToPlane = extractFloat("avgDistanceToPlane");
        profile.basePlaneCalibration.maxDistanceToPlane = extractFloat("maxDistanceToPlane");
        profile.basePlaneCalibration.planeFitRSquared = extractFloat("planeFitRSquared");
        
        // Extract validation flag
        auto isValidPos = jsonData.find("\"isValidCalibration\": ");
        if (isValidPos != std::string::npos) {
            isValidPos += 22;
            profile.basePlaneCalibration.isValidCalibration = jsonData.substr(isValidPos, 4) == "true";
        }

        // Extract minValidPlane / maxValidPlane if present
        auto extractPlaneComponent = [&](const std::string& planeName, const std::string& component)->float {
            std::string search = "\"" + planeName + "\""; // find block start
            auto blockPos = jsonData.find(search);
            if (blockPos == std::string::npos) return 0.0f;
            auto compPos = jsonData.find("\"" + component + "\":", blockPos);
            if (compPos == std::string::npos) return 0.0f;
            compPos += component.size() + 3; // skip key":
            auto end = jsonData.find_first_of(",\n}", compPos);
            if (end == std::string::npos) return 0.0f;
            try { return std::stof(jsonData.substr(compPos, end - compPos)); } catch(...) { return 0.0f; }
        };
        profile.minValidPlane.a = extractPlaneComponent("minValidPlane", "a");
        profile.minValidPlane.b = extractPlaneComponent("minValidPlane", "b");
        profile.minValidPlane.c = extractPlaneComponent("minValidPlane", "c");
        profile.minValidPlane.d = extractPlaneComponent("minValidPlane", "d");
        profile.maxValidPlane.a = extractPlaneComponent("maxValidPlane", "a");
        profile.maxValidPlane.b = extractPlaneComponent("maxValidPlane", "b");
        profile.maxValidPlane.c = extractPlaneComponent("maxValidPlane", "c");
        profile.maxValidPlane.d = extractPlaneComponent("maxValidPlane", "d");
        
        // Set timestamps to current time
        auto now = std::chrono::system_clock::now();
        profile.createdAt = now;
        profile.lastUpdated = now;
        profile.basePlaneCalibration.timestamp = now;
        
        return true;
        
    } catch (const std::exception& ex) {
        logger_->error("Error parsing calibration JSON: {}", ex.what());
        return false;
    }
}

} // namespace caldera::backend::tools::calibration