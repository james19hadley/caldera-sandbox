#include "tools/KinectDataViewer.h"
#include "hal/KinectV2_Device.h"
#include "hal/SensorRecorder.h"
#include "hal/MockSensorDevice.h"
#include "hal/ISensorDevice.h"
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
    : sensorType_(type), viewMode_(mode), kinect_device_(nullptr) {
    
    // Auto-detect or create specified sensor type
    if (sensorType_ == SensorType::AUTO_DETECT) {
        // For now, try Kinect V2 first
        // TODO: Add detection logic for multiple sensor types
        sensorType_ = SensorType::KINECT_V2;
    }
    
    switch (sensorType_) {
        case SensorType::KINECT_V2:
            kinect_device_ = new caldera::backend::hal::KinectV2_Device();
            std::cout << "Initializing Kinect V2 sensor..." << std::endl;
            break;
        case SensorType::KINECT_V1:
            // TODO: Implement when KinectV1_Device is ready
            std::cerr << "Kinect V1 support not yet implemented" << std::endl;
            kinect_device_ = nullptr;
            break;
        case SensorType::PLAYBACK_FILE:
            // Should not reach here - use playback constructor
            std::cerr << "Error: Use playback constructor for file playback" << std::endl;
            kinect_device_ = nullptr;
            break;
        default:
            kinect_device_ = nullptr;
            break;
    }
}

KinectDataViewer::KinectDataViewer(const std::string& dataFile, ViewMode mode)
    : sensorType_(SensorType::PLAYBACK_FILE), viewMode_(mode), kinect_device_(nullptr), playback_file_(dataFile) {
    
    std::cout << "Initializing playback from file: " << dataFile << std::endl;
    
    mock_device_ = std::make_unique<caldera::backend::hal::MockSensorDevice>(dataFile);
    
    // Set default playback options (ONCE - play through data exactly once)
    mock_device_->setPlaybackMode(caldera::backend::hal::MockSensorDevice::PlaybackMode::ONCE);
    mock_device_->setPlaybackFPS(30.0);
}

KinectDataViewer::~KinectDataViewer() noexcept {
    try {
        // Minimal destructor to avoid terminate call
        // All cleanup should be done explicitly via stop() before destruction
        running_.store(false);
        
        // Explicitly reset smart pointers to control destruction order
        recorder_.reset();
        mock_device_.reset();
        viewer_thread_.reset();
    } catch (...) {
        // Swallow all exceptions in destructor to prevent terminate()
    }
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

    auto* device = this->getCurrentDevice();
    if (!device) {
        std::cerr << "No sensor device available" << std::endl;
        return false;
    }

    // Try to open device
    if (!device->open()) {
        std::cerr << "Failed to open sensor device" << std::endl;
        return false;
    }

    // Set up frame callback
    device->setFrameCallback([this](const RawDepthFrame& depth, const RawColorFrame& color) {
        if (viewMode_ == ViewMode::TEXT_ONLY) {
            printFrame("DEPTH", depth.width, depth.height, 
                      depth.width * depth.height * sizeof(uint16_t), depth.timestamp_ns);
            printFrame("COLOR", color.width, color.height,
                      color.data.size(), color.timestamp_ns);
        }
        
        // Record frame if recording is active
        if (recorder_ && recorder_->isRecording()) {
            recorder_->recordFrame(depth, color);
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
    std::cout << "Device ID: " << device->getDeviceID() << std::endl;
    
    // Print playback info for file mode
    if (sensorType_ == SensorType::PLAYBACK_FILE) {
        std::cout << "Playback file: " << playback_file_ << std::endl;
        std::cout << "Frame count: " << getPlaybackFrameCount() << std::endl;
    }
    
    return true;
}

void KinectDataViewer::stop() {
    // Prevent multiple calls to stop()
    bool expected = false;
    if (!stop_called_.compare_exchange_strong(expected, true)) {
        return; // stop() was already called
    }
    
    if (!running_.load()) {
        return;
    }

    running_.store(false);
    
    // Close device first to signal thread to stop
    try {
        auto* device = this->getCurrentDevice();
        if (device) {
            device->close();
        }
    } catch (...) {
        // Ignore device close errors during shutdown
    }
    
    // Then wait for thread to finish
    try {
        if (viewer_thread_ && viewer_thread_->joinable()) {
            viewer_thread_->join();
        }
    } catch (...) {
        // Thread join failed, but don't crash the program
    }
    
    std::cout << "\nKinect Data Viewer stopped." << std::endl;
}

bool KinectDataViewer::isRunning() const {
    if (!running_.load()) {
        return false;
    }
    
    // For playback mode, also check if the mock device is still running
    if (sensorType_ == SensorType::PLAYBACK_FILE && mock_device_) {
        return mock_device_->isRunning();
    }
    
    return true;
}

void KinectDataViewer::runFor(int seconds) {
    if (!start()) {
        return;
    }

    try {
        if (seconds > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
        } else {
            // Run until stopped externally
            while (isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Always call stop explicitly to ensure clean shutdown
        stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Error in runFor: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error in runFor" << std::endl;
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
            auto* device = this->getCurrentDevice();
            std::cout << "Device running: " << (device && device->isRunning() ? "YES" : "NO") << std::endl;
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

bool KinectDataViewer::startRecording(const std::string& filename) {
    if (recorder_) {
        std::cout << "Recording already active" << std::endl;
        return false;
    }

    recorder_ = std::make_unique<caldera::backend::hal::SensorRecorder>(filename);
    if (!recorder_->startRecording()) {
        recorder_.reset();
        return false;
    }

    std::cout << "Started recording to: " << filename << std::endl;
    return true;
}

void KinectDataViewer::stopRecording() {
    if (recorder_) {
        recorder_->stopRecording();
        std::cout << "Recording stopped. Frames: " << recorder_->getFrameCount() << std::endl;
        recorder_.reset();
    }
}

bool KinectDataViewer::isRecording() const {
    return recorder_ && recorder_->isRecording();
}

void KinectDataViewer::setPlaybackOptions(bool loop, double fps) {
    if (sensorType_ != SensorType::PLAYBACK_FILE || !mock_device_) {
        return;
    }
    
    auto mode = loop ? caldera::backend::hal::MockSensorDevice::PlaybackMode::LOOP 
                    : caldera::backend::hal::MockSensorDevice::PlaybackMode::ONCE;
    mock_device_->setPlaybackMode(mode);
    mock_device_->setPlaybackFPS(fps);
}

size_t KinectDataViewer::getPlaybackFrameCount() const {
    if (sensorType_ != SensorType::PLAYBACK_FILE || !mock_device_) {
        return 0;
    }
    
    return mock_device_->getFrameCount();
}

caldera::backend::hal::ISensorDevice* caldera::backend::tools::KinectDataViewer::getCurrentDevice() const {
    if (sensorType_ == SensorType::PLAYBACK_FILE) {
        return mock_device_.get();
    } else {
        return kinect_device_;
    }
}

} // namespace caldera::backend::tools