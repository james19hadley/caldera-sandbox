#ifndef CALDERA_BACKEND_HAL_KINECTV2_DEVICE_H
#define CALDERA_BACKEND_HAL_KINECTV2_DEVICE_H

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <atomic>
#include <thread>
#include <memory>
#include <spdlog/spdlog.h>

#include "hal/ISensorDevice.h"
#include "common/Logger.h"

namespace caldera::backend::hal {

class KinectV2_Device : public ISensorDevice {
public:
    KinectV2_Device();
    virtual ~KinectV2_Device() override;

    // ISensorDevice interface implementation
    bool open() override;
    void close() override;
    bool isRunning() const override;
    std::string getDeviceID() const override;
    void setFrameCallback(RawFrameCallback callback) override;

private:
    void captureLoop();

    std::shared_ptr<spdlog::logger> logger_;
    
    libfreenect2::Freenect2 freenect2_;
    libfreenect2::Freenect2Device* device_ = nullptr;
    libfreenect2::SyncMultiFrameListener* listener_ = nullptr;
    
    std::string serial_ = "";
    std::atomic<bool> is_running_ = {false};
    std::thread capture_thread_;
    RawFrameCallback frame_callback_ = nullptr;
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_KINECTV2_DEVICE_H