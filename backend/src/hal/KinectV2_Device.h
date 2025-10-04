#ifndef CALDERA_BACKEND_HAL_KINECTV2_DEVICE_H
#define CALDERA_BACKEND_HAL_KINECTV2_DEVICE_H

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/packet_pipeline.h>
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
    
    // IMPORTANT: libfreenect2 takes ownership of the PacketPipeline pointer and will
    // delete it inside Freenect2DeviceImpl's destructor. We therefore must NOT delete
    // it ourselves. Store as a non-owning raw pointer (intentionally leaked from our
    // perspective; real delete happens in libfreenect2).
    libfreenect2::PacketPipeline* pipeline_ = nullptr; // non-owning
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