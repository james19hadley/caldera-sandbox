#ifndef CALDERA_BACKEND_HAL_SENSOR_RECORDER_H
#define CALDERA_BACKEND_HAL_SENSOR_RECORDER_H

#include "hal/ISensorDevice.h"
#include "common/DataTypes.h"
#include <string>
#include <fstream>
#include <memory>
#include <spdlog/spdlog.h>

namespace caldera::backend::hal {

/**
 * SensorRecorder records sensor data to files for later playback in tests.
 * 
 * File format:
 * - Header: magic number, version, frame count
 * - For each frame: timestamp, depth data, color data
 * 
 * Usage:
 *   SensorRecorder recorder("test_data.dat");
 *   recorder.startRecording();
 *   // ... feed frames via recordFrame()
 *   recorder.stopRecording();
 */
class SensorRecorder {
public:
    explicit SensorRecorder(const std::string& filename);
    ~SensorRecorder();

    // Start/stop recording
    bool startRecording();
    void stopRecording();
    bool isRecording() const { return is_recording_; }

    // Record a frame (called from sensor callback)
    void recordFrame(const common::RawDepthFrame& depth, const common::RawColorFrame& color);

    // Get stats
    size_t getFrameCount() const { return frame_count_; }
    size_t getFileSizeBytes() const;

private:
    void writeHeader();
    void updateFrameCount();

    std::string filename_;
    std::ofstream file_;
    bool is_recording_ = false;
    size_t frame_count_ = 0;
    std::shared_ptr<spdlog::logger> logger_;

    // File format constants
    static constexpr uint32_t MAGIC_NUMBER = 0x4B494E54; // "KINT"
    static constexpr uint32_t FILE_VERSION = 1;
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_SENSOR_RECORDER_H