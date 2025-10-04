#ifndef CALDERA_BACKEND_HAL_KINECTV1_DEVICE_H
#define CALDERA_BACKEND_HAL_KINECTV1_DEVICE_H

#include "hal/ISensorDevice.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#ifdef __has_include
#  if __has_include(<libfreenect/libfreenect.h>)
#    include <libfreenect/libfreenect.h>
#  elif __has_include(<libfreenect.h>)
#    include <libfreenect.h>
#  else
#    warning "libfreenect headers not found; KinectV1_Device will be stubbed"
typedef struct freenect_context freenect_context; // fallback minimal forward decls
typedef struct freenect_device freenect_device;
extern "C" { int freenect_init(freenect_context**, void*); int freenect_num_devices(freenect_context*); int freenect_open_device(freenect_context*, freenect_device**, int); int freenect_start_depth(freenect_device*); int freenect_start_video(freenect_device*); int freenect_process_events(freenect_context*); void freenect_stop_depth(freenect_device*); void freenect_stop_video(freenect_device*); void freenect_close_device(freenect_device*); void freenect_shutdown(freenect_context*); void freenect_set_log_level(freenect_context*, int); void freenect_set_depth_callback(freenect_device*, void(*)(freenect_device*, void*, uint32_t)); void freenect_set_video_callback(freenect_device*, void(*)(freenect_device*, void*, uint32_t)); void freenect_set_user(freenect_device*, void*); void* freenect_get_user(freenect_device*); int freenect_set_depth_format(freenect_device*, int); int freenect_set_video_format(freenect_device*, int); }
#  endif
#else
#  include <libfreenect/libfreenect.h>
#endif
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <string>

namespace caldera::backend::hal {

class KinectV1_Device : public ISensorDevice {
public:
    KinectV1_Device();
    ~KinectV1_Device() override;

    bool open() override;
    void close() override;
    bool isRunning() const override;
    std::string getDeviceID() const override;
    void setFrameCallback(RawFrameCallback callback) override;

private:
    // C-style callback trampolines
    static void depth_callback(freenect_device* dev, void* depth, uint32_t timestamp);
    static void video_callback(freenect_device* dev, void* video, uint32_t timestamp);

    // Internal processing functions
    void processDepthFrame(void* depth, uint32_t timestamp);
    void processColorFrame(void* video, uint32_t timestamp);

    void captureLoop();

    freenect_context* freenect_context_ = nullptr;
    freenect_device*  freenect_device_  = nullptr;

    std::shared_ptr<spdlog::logger> logger_;

    std::thread capture_thread_;
    std::atomic<bool> is_running_{false};

    RawFrameCallback frame_callback_;

    // Frame assembly buffers (reused to avoid realloc each callback)
    common::RawDepthFrame  pending_depth_;
    common::RawColorFrame  pending_color_;
    std::atomic<bool> depth_ready_{false};
    std::atomic<bool> color_ready_{false};

    std::string device_serial_;
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_KINECTV1_DEVICE_H
