#include "tools/KinectDataViewer.h"
#include "hal/KinectV2_Device.h"
#include "common/DataTypes.h"
#include "common/Logger.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <thread>

namespace caldera::backend::tools {

using namespace caldera::backend::hal;
using namespace caldera::backend::common;

KinectDataViewer::KinectDataViewer(SensorType type, ViewMode mode) 
    : sensorType_(type), viewMode_(mode), device_(nullptr) {
    
    // Auto-detect or create specified sensor type
    if (sensorType_ == SensorType::AUTO_DETECT) {
        // For now, try Kinect V2 first
        // TODO: Add detection logic for multiple sensor types
        sensorType_ = SensorType::KINECT_V2;
    }
    
    switch (sensorType_) {
        case SensorType::KINECT_V2:
            device_ = new caldera::backend::hal::KinectV2_Device();
            std::cout << "Initializing Kinect V2 sensor..." << std::endl;
            break;
        case SensorType::KINECT_V1:
            // TODO: Implement when KinectV1_Device is ready
            std::cerr << "Kinect V1 support not yet implemented" << std::endl;
            device_ = nullptr;
            break;
        default:
            device_ = nullptr;
            break;
    }
}

KinectDataViewer::~KinectDataViewer() {
    stop();
    delete device_;
}

void KinectDataViewer::setDepthFrameCallback(DepthFrameCallback callback) {
    depth_callback_ = callback;
}

void KinectDataViewer::setColorFrameCallback(ColorFrameCallback callback) {
    color_callback_ = callback;
}

bool KinectDataViewer::start() {
    if (running_.load()) {
        return true; // Already running
    }

    if (!device_) {
        std::cerr << "No sensor device available" << std::endl;
        return false;
    }

    // Try to open device
    if (!device_->open()) {
        std::cerr << "Failed to open sensor device" << std::endl;
        return false;
    }

    // Set up frame callback
    device_->setFrameCallback([this](const RawDepthFrame& depth, const RawColorFrame& color) {
        if (viewMode_ == ViewMode::TEXT_ONLY) {
            printFrame("DEPTH", depth.width, depth.height, 
                      depth.width * depth.height * sizeof(uint16_t), depth.timestamp_ns);
            printFrame("COLOR", color.width, color.height,
                      color.data.size(), color.timestamp_ns);
        }
        
        // Call external callbacks if set
        if (depth_callback_) {
            depth_callback_(depth);
        }
        if (color_callback_) {
            color_callback_(color);
        }
    });

    running_.store(true);
    viewer_thread_ = std::make_unique<std::thread>(&KinectDataViewer::viewerLoop, this);
    
    std::cout << "Kinect Data Viewer started. Press Ctrl+C to stop." << std::endl;
    std::cout << "Device ID: " << device_->getDeviceID() << std::endl;
    return true;
}

void KinectDataViewer::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);
    
    if (viewer_thread_ && viewer_thread_->joinable()) {
        viewer_thread_->join();
    }
    
    device_->close();
    std::cout << "\nKinect Data Viewer stopped." << std::endl;
}

bool KinectDataViewer::isRunning() const {
    return running_.load();
}

void KinectDataViewer::runFor(int seconds) {
    if (!start()) {
        return;
    }

    if (seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        stop();
    } else {
        // Run until stopped externally
        while (isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void KinectDataViewer::viewerLoop() {
    auto last_print = std::chrono::steady_clock::now();
    int frame_count = 0;
    
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print);
        
        if (elapsed.count() >= 5) {
            std::cout << "\n=== Viewer Status ===" << std::endl;
            std::cout << "Running for " << elapsed.count() << " seconds" << std::endl;
            std::cout << "Frames processed: " << frame_count << std::endl;
            std::cout << "Device running: " << (device_->isRunning() ? "YES" : "NO") << std::endl;
            last_print = now;
        }
        
        frame_count++;
    }
}

void KinectDataViewer::printFrame(const std::string& type, size_t width, size_t height, 
                                 size_t dataSize, uint64_t timestamp) {
    static int depth_count = 0, color_count = 0;
    
    if (type == "DEPTH") {
        if (++depth_count % 30 == 0) { // Print every 30th frame (~1 per second at 30fps)
            std::cout << "[" << std::setw(5) << depth_count << "] "
                     << type << ": " << width << "x" << height 
                     << ", " << dataSize << " bytes, ts=" << timestamp << std::endl;
        }
    } else if (type == "COLOR") {
        if (++color_count % 30 == 0) {
            std::cout << "[" << std::setw(5) << color_count << "] "
                     << type << ": " << width << "x" << height 
                     << ", " << dataSize << " bytes, ts=" << timestamp << std::endl;
        }
    }
}



} // namespace caldera::backend::tools