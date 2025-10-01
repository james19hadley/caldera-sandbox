#include "KinectV2_Device.h"
#include "common/LoggingNames.h"
#include <chrono>
#include <cstring>

namespace caldera::backend::hal {

KinectV2_Device::KinectV2_Device() {
    // Получаем базовый логгер - после открытия устройства создадим device-specific логгер
    logger_ = caldera::backend::common::Logger::instance().get(caldera::backend::logging_names::HAL_KINECT_V2);
}

KinectV2_Device::~KinectV2_Device() {
    close();
}

bool KinectV2_Device::open() {
    if (isRunning()) {
        logger_->warn("KinectV2_Device is already running");
        return true;
    }

    // Check for connected devices
    int deviceCount = freenect2_.enumerateDevices();
    if (deviceCount == 0) {
        logger_->critical("No Kinect devices found");
        return false;
    }

    // Get serial of first device
    serial_ = freenect2_.getDefaultDeviceSerialNumber();
    if (serial_.empty()) {
        logger_->critical("Failed to get device serial number");
        return false;
    }

    // Create device-specific logger with serial number
    std::string loggerName = std::string(caldera::backend::logging_names::HAL_KINECT_V2) + "." + serial_;
    logger_ = caldera::backend::common::Logger::instance().get(loggerName);

    // Open the device
    device_ = freenect2_.openDevice(serial_);
    if (!device_) {
        logger_->critical("Failed to open Kinect device with serial: {}", serial_);
        return false;
    }

    // Create frame listener for color and depth
    listener_ = new libfreenect2::SyncMultiFrameListener(libfreenect2::Frame::Color | libfreenect2::Frame::Depth);
    
    // Set listener on device
    device_->setColorFrameListener(listener_);
    device_->setIrAndDepthFrameListener(listener_);

    // Start the device
    if (!device_->start()) {
        logger_->critical("Failed to start Kinect device");
        delete listener_;
        listener_ = nullptr;
        device_->close();
        device_ = nullptr;
        return false;
    }

    // Set running flag and start capture thread (join on close)
    is_running_ = true;
    capture_thread_ = std::thread(&KinectV2_Device::captureLoop, this);

    logger_->info("KinectV2_Device successfully started with serial: {}", serial_);
    return true;
}

void KinectV2_Device::close() {
    if (!isRunning()) {
        return;
    }

    // Signal the capture thread to stop and join
    is_running_ = false;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    // Stop and close the device
    if (device_) {
        device_->stop();
        device_->close();
        device_ = nullptr;
    }

    // Clean up the listener
    if (listener_) {
        delete listener_;
        listener_ = nullptr;
    }

    logger_->info("KinectV2_Device shutdown completed");
}

void KinectV2_Device::captureLoop() {
    logger_->debug("KinectV2_Device capture loop started");
    
    while (is_running_) {
        libfreenect2::FrameMap frames;
        
        // Wait for new frame with 10 second timeout
        if (!listener_->waitForNewFrame(frames, 10000)) {
            logger_->error("KinectV2_Device timeout waiting for frame!");
            continue;
        }
        
        // Get color and depth frames
        libfreenect2::Frame *colorFrame = frames[libfreenect2::Frame::Color];
        libfreenect2::Frame *depthFrame = frames[libfreenect2::Frame::Depth];
        
        // If we have a callback, create our frame structures
        if (frame_callback_) {
            // Create RawDepthFrame
            RawDepthFrame rawDepth;
            rawDepth.sensorId = serial_;
            rawDepth.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            if (depthFrame) {
                rawDepth.width = static_cast<int>(depthFrame->width);
                rawDepth.height = static_cast<int>(depthFrame->height);
                size_t dataSize = rawDepth.width * rawDepth.height;
                rawDepth.data.resize(dataSize);
                
                // Copy depth data (libfreenect2 depth is usually float, but we need uint16_t)
                const float* depthData = reinterpret_cast<const float*>(depthFrame->data);
                for (size_t i = 0; i < dataSize; ++i) {
                    rawDepth.data[i] = static_cast<uint16_t>(std::min(depthData[i], 65535.0f));
                }
            }
            
            // Create RawColorFrame
            RawColorFrame rawColor;
            rawColor.sensorId = serial_;
            rawColor.timestamp_ns = rawDepth.timestamp_ns;
            
            if (colorFrame) {
                rawColor.width = static_cast<int>(colorFrame->width);
                rawColor.height = static_cast<int>(colorFrame->height);
                size_t dataSize = rawColor.width * rawColor.height * colorFrame->bytes_per_pixel;
                rawColor.data.resize(dataSize);
                
                // Copy color data
                std::memcpy(rawColor.data.data(), colorFrame->data, dataSize);
            }
            
            // Call the callback with both frames
            frame_callback_(rawDepth, rawColor);
        }
        
        // Crucial: release frames to prevent memory leaks
        listener_->release(frames);
    }
    
    logger_->debug("KinectV2_Device capture loop ended");
}

bool KinectV2_Device::isRunning() const {
    return is_running_.load();
}

std::string KinectV2_Device::getDeviceID() const {
    return serial_;
}

void KinectV2_Device::setFrameCallback(RawFrameCallback callback) {
    frame_callback_ = std::move(callback);
}

} // namespace caldera::backend::hal