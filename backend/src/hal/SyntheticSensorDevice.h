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

    struct FaultInjectionConfig {
        uint32_t dropEveryN = 0;    // Skip emitting every Nth produced frame if >0
        uint32_t jitterMaxMs = 0;   // Uniform random delay [0,jitterMaxMs] ms before emit
        uint32_t seed = 0xC0FFEE;   // RNG seed for deterministic test runs
    };

    explicit SyntheticSensorDevice(const Config& cfg, std::shared_ptr<spdlog::logger> log);
    ~SyntheticSensorDevice() override { close(); }

    bool open() override;
    void close() override;
    bool isRunning() const override { return running_.load(); }
    std::string getDeviceID() const override { return cfg_.sensorId; }
    void setFrameCallback(RawFrameCallback callback) override { callback_ = std::move(callback); }
    // Control hooks for fault-injection phases (test-only usage):
    void pause();
    void resume();
    void setStopAfter(uint64_t frames); // automatically pauses after emitting >= frames
    uint64_t framesGenerated() const { return frame_counter_; }
    bool isPaused() const { return paused_.load(); }
    void configureFaultInjection(const FaultInjectionConfig& fic);
    struct Stats { uint64_t produced=0; uint64_t emitted=0; uint64_t dropped=0; };
    Stats stats() const { return Stats{produced_frames_.load(), emitted_frames_.load(), dropped_frames_.load()}; }

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
    std::atomic<bool> paused_{false};
    std::thread worker_;
    uint64_t frame_counter_ = 0;
    uint32_t base_checksum_ = 0; // checksum of static spatial pattern (ignoring frame counter)
    std::atomic<uint64_t> stop_after_{0};
    // Fault injection state
    std::atomic<uint32_t> fi_dropEveryN_{0};
    std::atomic<uint32_t> fi_jitterMaxMs_{0};
    std::atomic<uint32_t> fi_seed_{0};
    std::atomic<bool> fi_rng_init_{false};
    std::atomic<uint64_t> produced_frames_{0};
    std::atomic<uint64_t> emitted_frames_{0};
    std::atomic<uint64_t> dropped_frames_{0};
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_SYNTHETIC_SENSOR_DEVICE_H
