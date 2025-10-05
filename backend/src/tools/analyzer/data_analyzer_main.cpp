#include "tools/viewer/SensorViewerCore.h"
#include "processing/DepthCorrector.h"
#include "processing/CoordinateTransform.h"
#include "tools/calibration/SensorCalibration.h"
#include "common/Logger.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <cstdlib>

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
        bool degraded = false;
        if (!depthCorrector_->loadProfile(sensorId)) {
            logger_->warn("Failed to load depth correction profile for {} - continuing in degraded mode", sensorId);
            degraded = true;
        }

        // Load calibration for coordinate transform, but don't fail if missing
        tools::calibration::SensorCalibration calibrator;
        tools::calibration::SensorCalibrationProfile calibProfile;
        if (!calibrator.loadCalibrationProfile(sensorId, calibProfile)) {
            logger_->warn("Failed to load calibration profile for {} - continuing in degraded mode", sensorId);
            degraded = true;
        } else {
            if (!coordinateTransform_->loadFromCalibration(calibProfile)) {
                logger_->warn("Failed to load coordinate transform calibration - continuing in degraded mode");
                degraded = true;
            }
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
    std::vector<double> perFrameMeans; perFrameMeans.reserve(maxFramesToAnalyze);
    auto tStart = std::chrono::steady_clock::now();
        
        // Set up depth frame callback
        viewer->setDepthFrameCallback([this, &frameCount, &perFrameMeans, &tStart, maxFramesToAnalyze](const common::RawDepthFrame& rawFrame) {
            if (frameCount >= maxFramesToAnalyze) return;
            logger_->info("=== Analyzing Frame {} ===", frameCount + 1);
            // Record simple per-frame mean (for noise/variability)
            double sum = 0.0; size_t count = 0;
            for (auto v : rawFrame.data) { if (v != 0) { sum += v; ++count; } }
            double mean = (count>0) ? (sum / static_cast<double>(count)) : 0.0;
            perFrameMeans.push_back(mean);
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
        auto tEnd = std::chrono::steady_clock::now();
        double secs = std::chrono::duration_cast<std::chrono::duration<double>>(tEnd - tStart).count();
        double fps = (secs > 0.0 && frameCount>0) ? (static_cast<double>(frameCount)/secs) : 0.0;
        // Compute mean/stddev of per-frame means
        double overallMean = 0.0, variance = 0.0;
        if (!perFrameMeans.empty()) {
            for (double m : perFrameMeans) overallMean += m;
            overallMean /= static_cast<double>(perFrameMeans.size());
            for (double m : perFrameMeans) variance += (m - overallMean)*(m - overallMean);
            variance = variance / static_cast<double>(perFrameMeans.size());
        }
        double stddev = sqrt(variance);
        // Convert mean/stddev from raw sensor units (uint16 depth, e.g. millimeters) to meters
        double overallMeanMeters = overallMean / 1000.0;
        double stddevMeters = stddev / 1000.0;

        logger_->info("Analysis complete. Processed {} frames in {:.3f}s (fps={:.2f})", frameCount, secs, fps);
        logger_->info("ANALYSIS_SUMMARY: {{\"frames\":{},\"elapsed_s\":{:.3f},\"fps\":{:.2f},\"mean_depth\":{:.3f},\"stddev_depth\":{:.3f}}}", frameCount, secs, fps, overallMeanMeters, stddevMeters);
        // Force flush of logger so external processes (tests) can read the summary immediately
        try {
            logger_->flush();
        } catch(...) {
            // best-effort
        }
        
        // Also write a deterministic sidecar JSON summary if requested via env
        const char* sidecar_env = std::getenv("CALDERA_ANALYSIS_SIDECAR");
        if (sidecar_env && sidecar_env[0] != '\0') {
            try {
                std::ofstream sfile(sidecar_env);
                if (sfile.is_open()) {
                    sfile << "{";
                    sfile << "\"frames\":" << frameCount << ",";
                    sfile << "\"elapsed_s\":" << std::fixed << std::setprecision(3) << secs << ",";
                    sfile << "\"fps\":" << std::fixed << std::setprecision(2) << fps << ",";
                    sfile << "\"mean_depth\":" << std::fixed << std::setprecision(3) << overallMeanMeters << ",";
                    sfile << "\"stddev_depth\":" << std::fixed << std::setprecision(3) << stddevMeters;
                    sfile << "}" << std::endl;
                    sfile.flush();
                    sfile.close();
                    logger_->info("Wrote analysis sidecar: {}", sidecar_env);
                } else {
                    logger_->error("Failed to open sidecar file for writing: {}", sidecar_env);
                }
            } catch (const std::exception& e) {
                logger_->error("Exception while writing sidecar: {}", e.what());
            }
        }
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

    // Ensure logger flush/shutdown so external processes can reliably read the log file
    try {
        caldera::backend::common::Logger::instance().shutdown();
    } catch(...) {
        // best-effort
    }

    return 0;
}