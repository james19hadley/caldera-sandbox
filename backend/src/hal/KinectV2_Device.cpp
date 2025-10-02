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
    // Ensure resources are released. close() is idempotent via isRunning() guard.
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

    // Optional pipeline override (only 'cpu' supported explicitly). Otherwise let libfreenect2 auto-detect.
    const char* pipeline_env = std::getenv("CALDERA_KINECT_V2_PIPELINE");
    std::string pipeline_choice = pipeline_env ? pipeline_env : "";
    for (auto &c : pipeline_choice) c = (char)tolower(c);
    if (pipeline_choice == "cpu") {
        logger_->info("Using forced CPU packet pipeline (CALDERA_KINECT_V2_PIPELINE=cpu)");
        pipeline_ = new libfreenect2::CpuPacketPipeline();
        device_ = freenect2_.openDevice(serial_, pipeline_);
    } else if (!pipeline_choice.empty()) {
        logger_->warn("Unsupported CALDERA_KINECT_V2_PIPELINE='{}' (only 'cpu' recognized) - falling back to auto-detect", pipeline_choice);
        device_ = freenect2_.openDevice(serial_);
    } else {
        device_ = freenect2_.openDevice(serial_);
    }
    // Fallback: if auto-detect failed, try CPU pipeline once.
    if (!device_) {
        logger_->warn("Primary openDevice failed, retrying with CPU pipeline fallback");
        pipeline_ = new libfreenect2::CpuPacketPipeline();
        device_ = freenect2_.openDevice(serial_, pipeline_);
    }
    if (!device_) {
        logger_->critical("Failed to open Kinect device with serial: {}", serial_);
        return false;
    }

    // Diagnostics: verify pipeline type (best-effort RTTI)
    if (pipeline_choice == "cpu" && pipeline_) {
        std::string rtti_name = typeid(*pipeline_).name();
        if (rtti_name.find("CpuPacketPipeline") == std::string::npos) {
            logger_->warn("Requested CPU pipeline but actual pipeline type is '{}'", rtti_name);
        }
    }

    bool disable_color = false;
    if (const char* dc = std::getenv("CALDERA_KINECT_V2_DISABLE_COLOR")) {
        std::string v(dc); for (auto &c: v) c=(char)tolower(c);
        if (v=="1"||v=="true"||v=="yes"||v=="on") disable_color = true;
    }
    if (disable_color) {
        logger_->info("Color stream disabled via CALDERA_KINECT_V2_DISABLE_COLOR");
    }
    int frame_types = libfreenect2::Frame::Depth | (disable_color ? 0 : libfreenect2::Frame::Color);
    listener_ = new libfreenect2::SyncMultiFrameListener(frame_types);
    if (!disable_color) device_->setColorFrameListener(listener_);
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

    // Stop and close the device (do NOT delete pointer explicitly; libfreenect2 examples
    // only call stop()/close() and allow process teardown to reclaim memory). Deleting it
    // here appeared to contribute to shutdown instability.
    if (device_) {
        logger_->debug("KinectV2_Device::close stopping device");
        try { device_->stop(); } catch(...) { logger_->warn("Exception during device stop"); }
        try { device_->close(); } catch(...) { logger_->warn("Exception during device close"); }
        // Don't delete device explicitly; libfreenect2 manages device lifetime post-close.
        device_ = nullptr;
    }

    // Clean up the listener
    if (listener_) { delete listener_; listener_ = nullptr; }

    logger_->info("KinectV2_Device shutdown completed");
}

void KinectV2_Device::captureLoop() {
    logger_->debug("KinectV2_Device capture loop started");
    
    while (is_running_) {
        libfreenect2::FrameMap frames;
        
        // Wait for new frame with 10 second timeout
        if (!listener_ || !listener_->waitForNewFrame(frames, 10000)) {
            logger_->error("KinectV2_Device timeout waiting for frame!");
            continue;
        }
        
        // Get color and depth frames
    libfreenect2::Frame *colorFrame = frames.count(libfreenect2::Frame::Color) ? frames[libfreenect2::Frame::Color] : nullptr;
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
        if (listener_) listener_->release(frames);
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