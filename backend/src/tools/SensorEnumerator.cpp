// Sensor enumeration implementation
// Provides lightweight probing of available sensors (currently Kinect V2 + Kinect V1)
// Notes:
//  * Kinect V2 probing uses a minimal open/close cycle to retrieve the serial.
//  * Kinect V1 probing uses libfreenect enumeration directly (does not spin up capture threads).

#include "tools/SensorEnumerator.h"
#include "hal/KinectV2_Device.h" // needs full definition for open()/getDeviceID()

#include <memory>
#include <iostream>

#if CALDERA_HAVE_KINECT_V1
    // We only need the C API symbols to count / open briefly; prefer lightweight include logic.
    #ifdef __has_include
        #if __has_include(<libfreenect/libfreenect.h>)
            #include <libfreenect/libfreenect.h>
        #elif __has_include(<libfreenect.h>)
            #include <libfreenect.h>
        #endif
    #else
        #include <libfreenect/libfreenect.h>
    #endif
#endif

namespace caldera::backend::tools {

std::vector<SensorInfo> enumerateSensors() {
    std::vector<SensorInfo> out;
    // Probe Kinect V2: use libfreenect2 enumerate to avoid full start/stop overhead.
    try {
        libfreenect2::Freenect2 fn2;
        int count = fn2.enumerateDevices();
        if (count > 0) {
            std::string serial = fn2.getDefaultDeviceSerialNumber();
            if (!serial.empty()) {
                out.emplace_back(SensorInfo{SensorType::KINECT_V2, serial});
            }
        }
    } catch (...) {
        // Ignore failures.
    }

#if CALDERA_HAVE_KINECT_V1
    // Probe Kinect V1 using libfreenect enumeration if available
    try {
        freenect_context* ctx = nullptr;
        if (freenect_init(&ctx, nullptr) == 0 && ctx) {
            int count = freenect_num_devices(ctx);
            if (count > 0) {
                for (int i = 0; i < count; ++i) {
                    freenect_device* dev = nullptr;
                    if (freenect_open_device(ctx, &dev, i) == 0 && dev) {
                        // We don't rely on serial retrieval API (portability); use index-based id
                        std::string id = "v1-index-" + std::to_string(i);
                        out.emplace_back(SensorInfo{SensorType::KINECT_V1, id});
                        freenect_close_device(dev);
                    }
                }
            }
            freenect_shutdown(ctx);
        }
    } catch (...) { }
#endif
    return out;
}

} // namespace caldera::backend::tools
