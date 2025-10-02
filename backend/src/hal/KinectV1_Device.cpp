#include "hal/KinectV1_Device.h"
#include "common/LoggingNames.h"
#include <cstring>
#include <chrono>

#if !CALDERA_HAVE_KINECT_V1
// If built without libfreenect (CALDERA_HAVE_KINECT_V1=0), stub out methods to avoid link errors (open() returns false)
namespace caldera::backend::hal {
KinectV1_Device::KinectV1_Device(){ logger_ = common::Logger::instance().get("HAL.KinectV1"); }
KinectV1_Device::~KinectV1_Device(){ }
bool KinectV1_Device::open(){ logger_->error("KinectV1 support not compiled (libfreenect missing)"); return false; }
void KinectV1_Device::close(){}
bool KinectV1_Device::isRunning() const { return false; }
std::string KinectV1_Device::getDeviceID() const { return {}; }
void KinectV1_Device::setFrameCallback(RawFrameCallback cb){ frame_callback_ = std::move(cb); }
void KinectV1_Device::captureLoop(){}
void KinectV1_Device::depth_callback(freenect_device*, void*, uint32_t){}
void KinectV1_Device::video_callback(freenect_device*, void*, uint32_t){}
void KinectV1_Device::processDepthFrame(void*, uint32_t){}
void KinectV1_Device::processColorFrame(void*, uint32_t){}
} // namespace caldera::backend::hal
#else

namespace caldera::backend::hal {

KinectV1_Device::KinectV1_Device() { logger_ = common::Logger::instance().get("HAL.KinectV1"); }

KinectV1_Device::~KinectV1_Device() { close(); }

bool KinectV1_Device::open() {
    if (is_running_.load()) {
        logger_->warn("KinectV1 already running");
        return true;
    }
    if (freenect_init(&freenect_context_, nullptr) < 0) {
        logger_->critical("freenect_init failed");
        return false;
    }
    freenect_set_log_level(freenect_context_, FREENECT_LOG_ERROR);
    // Only the camera subdevice is needed for imaging
    freenect_select_subdevices(freenect_context_, FREENECT_DEVICE_CAMERA);

    // Enumerate camera devices and prefer specific serial if provided
    freenect_device_attributes* attr_list = nullptr;
    int attr_count = freenect_list_device_attributes(freenect_context_, &attr_list);
    if (attr_count > 0) {
        const char* want_serial = std::getenv("CALDERA_KINECT1_SERIAL");
        for (auto* it = attr_list; it; it = it->next) {
            if (it->camera_serial) {
                logger_->info("KinectV1 camera found: serial={} ", it->camera_serial);
                if (!device_serial_.empty()) continue;
                if (!want_serial || device_serial_.empty()) {
                    // Choose first by default; refined below if env provided
                    device_serial_ = it->camera_serial;
                }
                if (want_serial && std::string(it->camera_serial) == want_serial) {
                    device_serial_ = it->camera_serial;
                    break;
                }
            }
        }
    }
    int dev_count = freenect_num_devices(freenect_context_);
    if (dev_count <= 0) {
        logger_->critical("No Kinect v1 camera devices found. Hints: ensure external power is connected so 045e:02ae and 045e:02ad appear in lsusb; consider blacklisting gspca_kinect if it claims the camera.");
        if (attr_list) freenect_free_device_attributes(attr_list);
        freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }
    int open_rc = -1;
    if (!device_serial_.empty()) {
        open_rc = freenect_open_device_by_camera_serial(freenect_context_, &freenect_device_, device_serial_.c_str());
    } else {
        open_rc = freenect_open_device(freenect_context_, &freenect_device_, 0);
    }
    if (attr_list) { freenect_free_device_attributes(attr_list); attr_list = nullptr; }
    if (open_rc < 0) {
        logger_->critical("Failed to open Kinect v1 camera (serial='{}', index 0 fallback)", device_serial_);
        freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }

    // Register callbacks and user data
    freenect_set_user(freenect_device_, this);
    freenect_set_depth_callback(freenect_device_, &KinectV1_Device::depth_callback);
    freenect_set_video_callback(freenect_device_, &KinectV1_Device::video_callback);

    // Configure frame modes (RGB + Depth in mm @ VGA 640x480)
    freenect_frame_mode vmode = freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_RGB);
    freenect_frame_mode dmode = freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_MM);
    if (!vmode.is_valid || !dmode.is_valid) {
        logger_->critical("Requested video/depth modes not supported");
        freenect_close_device(freenect_device_); freenect_device_ = nullptr; freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }
    if (freenect_set_video_mode(freenect_device_, vmode) < 0) {
        logger_->critical("Failed to set video mode RGB 640x480");
        freenect_close_device(freenect_device_); freenect_device_ = nullptr; freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }
    if (freenect_set_depth_mode(freenect_device_, dmode) < 0) {
        logger_->critical("Failed to set depth mode MM 640x480");
        freenect_close_device(freenect_device_); freenect_device_ = nullptr; freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }

    if (freenect_start_depth(freenect_device_) < 0) {
        logger_->critical("Failed to start depth stream");
        freenect_close_device(freenect_device_); freenect_device_ = nullptr; freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }
    if (freenect_start_video(freenect_device_) < 0) {
        logger_->critical("Failed to start video stream");
        freenect_stop_depth(freenect_device_);
        freenect_close_device(freenect_device_); freenect_device_ = nullptr; freenect_shutdown(freenect_context_); freenect_context_ = nullptr; return false;
    }

    is_running_.store(true);
    capture_thread_ = std::thread(&KinectV1_Device::captureLoop, this);
    logger_->info("KinectV1 started");
    return true;
}

void KinectV1_Device::close() {
    if (!is_running_.load()) return;
    is_running_.store(false);
    if (capture_thread_.joinable()) capture_thread_.join();

    if (freenect_device_) {
        freenect_stop_video(freenect_device_);
        freenect_stop_depth(freenect_device_);
        freenect_close_device(freenect_device_);
        freenect_device_ = nullptr;
    }
    if (freenect_context_) {
        freenect_shutdown(freenect_context_);
        freenect_context_ = nullptr;
    }
    logger_->info("KinectV1 shutdown complete");
}

bool KinectV1_Device::isRunning() const { return is_running_.load(); }

std::string KinectV1_Device::getDeviceID() const { return device_serial_; }

void KinectV1_Device::setFrameCallback(RawFrameCallback cb) { frame_callback_ = std::move(cb); }

void KinectV1_Device::captureLoop() {
    while (is_running_.load() && freenect_context_) {
        if (freenect_process_events(freenect_context_) < 0) {
            logger_->error("freenect_process_events failed");
            break;
        }
    }
}

void KinectV1_Device::depth_callback(freenect_device* dev, void* depth, uint32_t timestamp) {
    auto* self = static_cast<KinectV1_Device*>(freenect_get_user(dev));
    if (self) self->processDepthFrame(depth, timestamp);
}

void KinectV1_Device::video_callback(freenect_device* dev, void* video, uint32_t timestamp) {
    auto* self = static_cast<KinectV1_Device*>(freenect_get_user(dev));
    if (self) self->processColorFrame(video, timestamp);
}

void KinectV1_Device::processDepthFrame(void* depth, uint32_t timestamp) {
    if (!frame_callback_) return;
    uint16_t* depthData = static_cast<uint16_t*>(depth);
    // Kinect v1 depth resolution (default): 640x480
    pending_depth_.sensorId = "KinectV1";
    // Convert provided device timestamp (in ms) to ns if plausible; else use steady clock
    uint64_t ts_ns = 0;
    if (timestamp > 0 && timestamp < (3600u * 1000u)) { // within an hour
        ts_ns = static_cast<uint64_t>(timestamp) * 1000000ull; // ms -> ns
    } else {
        ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    pending_depth_.timestamp_ns = ts_ns;
    pending_depth_.width = 640; // assume default
    pending_depth_.height = 480;
    pending_depth_.data.assign(depthData, depthData + (640*480));
    depth_ready_.store(true, std::memory_order_release);

    if (depth_ready_.load(std::memory_order_acquire) && color_ready_.load(std::memory_order_acquire)) {
        frame_callback_(pending_depth_, pending_color_);
        depth_ready_.store(false, std::memory_order_release);
        color_ready_.store(false, std::memory_order_release);
    }
}

void KinectV1_Device::processColorFrame(void* video, uint32_t timestamp) {
    if (!frame_callback_) return;
    uint8_t* rgb = static_cast<uint8_t*>(video);
    pending_color_.sensorId = "KinectV1";
    // Align to last depth if available, else synthesize from this callback's timestamp
    uint64_t ts_ns = pending_depth_.timestamp_ns;
    if (ts_ns == 0) {
        if (timestamp > 0 && timestamp < (3600u * 1000u)) ts_ns = static_cast<uint64_t>(timestamp) * 1000000ull;
        else ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    pending_color_.timestamp_ns = ts_ns;
    pending_color_.width = 640; // RGB at 640x480 by default
    pending_color_.height = 480;
    pending_color_.data.assign(rgb, rgb + (640*480*3)); // RGB format
    color_ready_.store(true, std::memory_order_release);

    if (depth_ready_.load(std::memory_order_acquire) && color_ready_.load(std::memory_order_acquire)) {
        frame_callback_(pending_depth_, pending_color_);
        depth_ready_.store(false, std::memory_order_release);
        color_ready_.store(false, std::memory_order_release);
    }
}

} // namespace caldera::backend::hal
#endif // FREENECT_DEPTH_MM
