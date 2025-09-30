#ifndef CALDERA_BACKEND_HAL_MOCK_SENSOR_DEVICE_H
#define CALDERA_BACKEND_HAL_MOCK_SENSOR_DEVICE_H

#include "hal/ISensorDevice.h"
#include "common/DataTypes.h"
#include <string>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <spdlog/spdlog.h>

namespace caldera::backend::hal {

/**
 * MockSensorDevice plays back recorded sensor data from files.
 * 
 * Supports:
 * - Single frame playback (for static tests)
 * - Loop playback (continuous cycling through frames)
 * - Controlled timing (real FPS or custom speed)
 * 
 * Usage:
 *   MockSensorDevice mock("recorded_data.dat");
 *   mock.setPlaybackMode(MockSensorDevice::PlaybackMode::LOOP);
 *   mock.open();
 *   mock.setFrameCallback(callback);
 */
class MockSensorDevice : public ISensorDevice {
public:
    enum class PlaybackMode {
        SINGLE_FRAME,    // Play first frame only
        ONCE,           // Play through all frames once
        LOOP            // Loop continuously
    };

    explicit MockSensorDevice(const std::string& dataFile);
    virtual ~MockSensorDevice() noexcept override;

    // ISensorDevice interface
    bool open() override;
    void close() override;
    bool isRunning() const override { return is_running_.load(); }
    std::string getDeviceID() const override { return "MockSensor_" + data_file_; }
    void setFrameCallback(RawFrameCallback callback) override;

    // Mock-specific configuration
    void setPlaybackMode(PlaybackMode mode) { playback_mode_ = mode; }
    void setPlaybackFPS(double fps) { playback_fps_ = fps; }
    void setLoopCount(int count) { loop_count_ = count; } // -1 = infinite

    // Data info
    size_t getFrameCount() const { return frame_count_; }
    bool isDataLoaded() const { return data_loaded_; }

private:
    struct FrameData {
        uint64_t timestamp_ns;
        common::RawDepthFrame depth;
        common::RawColorFrame color;
    };

    bool loadDataFile();
    void playbackLoop();
    bool readFrame(size_t index);

    std::string data_file_;
    std::vector<FrameData> frames_;
    size_t frame_count_ = 0;
    bool data_loaded_ = false;

    PlaybackMode playback_mode_ = PlaybackMode::ONCE;
    double playback_fps_ = 30.0;
    int loop_count_ = -1; // -1 = infinite

    std::atomic<bool> is_running_{false};
    std::thread playback_thread_;
    RawFrameCallback frame_callback_ = nullptr;
    
    std::shared_ptr<spdlog::logger> logger_;

    // File format constants (must match SensorRecorder)
    static constexpr uint32_t MAGIC_NUMBER = 0x4B494E54; // "KINT"
    static constexpr uint32_t FILE_VERSION = 1;
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_MOCK_SENSOR_DEVICE_H