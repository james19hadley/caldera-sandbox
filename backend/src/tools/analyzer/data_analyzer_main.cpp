#include "tools/viewer/SensorViewerCore.h"
#include "processing/DepthCorrector.h"
#include "processing/CoordinateTransform.h"
#include "tools/calibration/SensorCalibration.h"
#include "common/Logger.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace caldera::backend;

class DataAnalyzer {
public:
    DataAnalyzer() {
        logger_ = common::Logger::instance().get("DataAnalyzer");
        
        // Initialize processing pipeline components
        depthCorrector_ = std::make_unique<processing::DepthCorrector>();
        coordinateTransform_ = std::make_unique<processing::CoordinateTransform>();
    }
    
    bool initialize(const std::string& sensorId = "kinect-v1") {
        // Load profiles for processing pipeline
        if (!depthCorrector_->loadProfile(sensorId)) {
            logger_->error("Failed to load depth correction profile");
            return false;
        }
        
        // Load calibration for coordinate transform
        tools::calibration::SensorCalibration calibrator;
        tools::calibration::SensorCalibrationProfile calibProfile;
        
        if (!calibrator.loadCalibrationProfile(sensorId, calibProfile)) {
            logger_->error("Failed to load calibration profile");
            return false;
        }
        
        if (!coordinateTransform_->loadFromCalibration(calibProfile)) {
            logger_->error("Failed to load coordinate transform calibration");
            return false;
        }
        
        logger_->info("Data analyzer initialized successfully");
        return true;
    }
    
    void analyzeRecordedData(const std::string& filename, int maxFramesToAnalyze = 10) {
        logger_->info("Analyzing recorded data: {}", filename);
        
        // Create a playback viewer to read the recorded data
        auto viewer = std::make_unique<tools::SensorViewerCore>(
            filename,  // dataFile for playback constructor 
            tools::ViewMode::TEXT_ONLY
        );
        
        int frameCount = 0;
        
        // Set up depth frame callback
        viewer->setDepthFrameCallback([this, &frameCount, maxFramesToAnalyze](const common::RawDepthFrame& rawFrame) {
            if (frameCount >= maxFramesToAnalyze) return;
            
            logger_->info("=== Analyzing Frame {} ===", frameCount + 1);
            analyzeFrame(rawFrame, frameCount);
            frameCount++;
        });
        
        // Start playback
        if (!viewer->start()) {
            logger_->error("Failed to start playback of: {}", filename);
            return;
        }
        
        // Wait for analysis to complete
        while (frameCount < maxFramesToAnalyze && viewer->isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        viewer->stop();
        logger_->info("Analysis complete. Processed {} frames", frameCount);
    }

private:
    void analyzeFrame(const common::RawDepthFrame& rawFrame, int frameNumber) {
        logger_->debug("Raw frame: {}x{}, {} bytes", rawFrame.width, rawFrame.height, rawFrame.data.size());
        
        // Step 1: Apply depth correction
        common::RawDepthFrame correctedFrame = rawFrame;  // Copy for processing
        depthCorrector_->correctFrame(correctedFrame);
        
        // Step 2: Sample some pixels and transform to world coordinates
        analyzeSamplePixels(correctedFrame, frameNumber);
    }
    
    void analyzeSamplePixels(const common::RawDepthFrame& frame, int frameNumber) {
        // Sample pixels: center, corners, and a few random points
        struct SamplePoint { int x, y; std::string name; };
        std::vector<SamplePoint> samplePoints = {
            {320, 240, "center"},
            {100, 100, "top-left"},
            {540, 100, "top-right"}, 
            {100, 380, "bottom-left"},
            {540, 380, "bottom-right"},
            {320, 100, "top-center"},
            {320, 380, "bottom-center"}
        };
        
        logger_->info("Sample pixel analysis for frame {}:", frameNumber + 1);
        
        for (const auto& sample : samplePoints) {
            if (sample.x >= frame.width || sample.y >= frame.height) continue;
            
            int idx = sample.y * frame.width + sample.x;
            uint16_t rawDepthValue = frame.data[idx];
            
            if (rawDepthValue == 0) {
                logger_->info("  {}: no depth data", sample.name);
                continue;
            }
            
            // Transform to world coordinates
            common::Point3D worldPoint = coordinateTransform_->transformPixelToWorld(
                sample.x, sample.y, static_cast<float>(rawDepthValue)
            );
            
            logger_->info("  {}: pixel({},{}) depth={} → world({:.3f},{:.3f},{:.3f}) valid={}", 
                         sample.name, sample.x, sample.y, rawDepthValue,
                         worldPoint.x, worldPoint.y, worldPoint.z, worldPoint.valid);
        }
    }
    
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<processing::DepthCorrector> depthCorrector_;
    std::unique_ptr<processing::CoordinateTransform> coordinateTransform_;
};

int main(int argc, char* argv[]) {
    // Initialize logging
    common::Logger::instance().initialize("logs/data_analyzer.log");
    
    std::string dataFile = "real_data.dat";
    
    if (argc > 1) {
        dataFile = argv[1];
    }
    
    std::cout << "Data Analyzer - Analyzing: " << dataFile << std::endl;
    
    DataAnalyzer analyzer;
    if (!analyzer.initialize()) {
        std::cerr << "Failed to initialize analyzer" << std::endl;
        return 1;
    }
    
    analyzer.analyzeRecordedData(dataFile, 5);  // Analyze first 5 frames
    
    return 0;
}