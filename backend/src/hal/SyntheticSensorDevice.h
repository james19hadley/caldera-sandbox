// SyntheticSensorDevice.h
// Deterministic in-memory synthetic depth sensor for integration tests (Phase 0).

#ifndef CALDERA_BACKEND_HAL_SYNTHETIC_SENSOR_DEVICE_H
#define CALDERA_BACKEND_HAL_SYNTHETIC_SENSOR_DEVICE_H

#include "hal/ISensorDevice.h"
#include <atomic>
#include <thread>
#include <memory>
#include <spdlog/spdlog.h>

namespace caldera::backend::hal {

class SyntheticSensorDevice : public ISensorDevice {
public:
    enum class Pattern { RAMP, CONSTANT, CHECKER, STRIPES, RADIAL }; // STRIPES: horizontal bands; RADIAL: concentric gradient center-high
    struct Config {
        int width = 16;
        int height = 16;
        double fps = 30.0;
        Pattern pattern = Pattern::RAMP;
        uint16_t constantValue = 1000; // used if pattern == CONSTANT
        std::string sensorId = "Synthetic_0";
    };

    explicit SyntheticSensorDevice(const Config& cfg, std::shared_ptr<spdlog::logger> log);
    ~SyntheticSensorDevice() override { close(); }

    bool open() override;
    void close() override;
    bool isRunning() const override { return running_.load(); }
    std::string getDeviceID() const override { return cfg_.sensorId; }
    void setFrameCallback(RawFrameCallback callback) override { callback_ = std::move(callback); }

    // Expose base pattern checksum (for internal debugging only); tests should regenerate pattern independently.
    uint32_t basePatternChecksum() const { return base_checksum_; }

private:
    void runLoop();
    void fillPattern(std::vector<uint16_t>& buf) const;
    uint32_t computeCRC(const std::vector<uint16_t>& buf) const; // simple CRC32 over raw depth

    Config cfg_{};
    std::shared_ptr<spdlog::logger> log_;
    RawFrameCallback callback_{};
    std::atomic<bool> running_{false};
    std::thread worker_;
    uint64_t frame_counter_ = 0;
    uint32_t base_checksum_ = 0; // checksum of static spatial pattern (ignoring frame counter)
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_SYNTHETIC_SENSOR_DEVICE_H
