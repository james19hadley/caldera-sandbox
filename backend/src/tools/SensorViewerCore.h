#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <string>

namespace caldera::backend::hal {
    class KinectV2_Device;
    class KinectV1_Device;
    class SensorRecorder;
    class MockSensorDevice;
    class ISensorDevice;
}

namespace caldera::backend::common {
    struct RawDepthFrame;
    struct RawColorFrame;
}

namespace caldera::backend::tools {

enum class SensorType {
    KINECT_V2,
    KINECT_V1,  // Future support
    AUTO_DETECT,
    PLAYBACK_FILE  // Playback recorded data
};

enum class ViewMode {
    TEXT_ONLY,
    VISUAL_WINDOW
};

// Callback function types for frame data
using DepthFrameCallback = std::function<void(const caldera::backend::common::RawDepthFrame&)>;
using ColorFrameCallback = std::function<void(const caldera::backend::common::RawColorFrame&)>;

/**
 * @brief Universal Kinect data viewer for debugging and testing
 * 
 * This utility opens available Kinect devices and displays depth/color data.
 * Supports Kinect V2 (current) and prepared for Kinect V1 (future).
 */
class SensorViewerCore {
public:
    explicit SensorViewerCore(SensorType type = SensorType::AUTO_DETECT, ViewMode mode = ViewMode::TEXT_ONLY);
    
    /**
     * @brief Constructor for playback mode
     * @param dataFile Path to recorded data file 
     * @param mode View mode
     */
    explicit SensorViewerCore(const std::string& dataFile, ViewMode mode = ViewMode::TEXT_ONLY);
    
    ~SensorViewerCore() noexcept;

    /**
     * @brief Start the viewer
     * @return true if successfully started
     */
    bool start();

    /**
     * @brief Stop the viewer
     */
    void stop();

    /**
     * @brief Check if viewer is running
     */
    bool isRunning() const;

    /**
     * @brief Run viewer for specified duration (blocking)
     * @param seconds Duration to run (0 = run until stopped)
     */
    void runFor(int seconds = 0);

    /**
     * @brief Set callback for depth frame data
     */
    void setDepthFrameCallback(DepthFrameCallback callback);

    /**
     * @brief Set callback for color frame data
     */
    void setColorFrameCallback(ColorFrameCallback callback);

    /**
     * @brief Enable recording to file
     * @param filename File to record to
     * @return true if recording started successfully
     */
    bool startRecording(const std::string& filename);

    /**
     * @brief Stop recording
     */
    void stopRecording();

    /**
     * @brief Check if currently recording
     */
    bool isRecording() const;

    /**
     * @brief Set playback options (only for playback mode)
     * @param loop Whether to loop playback
     * @param fps Playback frame rate (0 = original timing)
     */
    void setPlaybackOptions(bool loop = false, double fps = 30.0);

    /**
     * @brief Get playback info (only for playback mode)
     */
    size_t getPlaybackFrameCount() const;

private:
    void viewerLoop();
    void printFrame(const std::string& type, size_t width, size_t height, 
                   size_t dataSize, uint64_t timestamp);

    SensorType sensorType_;
    ViewMode viewMode_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_called_{false};
    std::unique_ptr<std::thread> viewer_thread_;
    
    // Active live sensor device (v1 or v2) when not in playback mode
    std::unique_ptr<caldera::backend::hal::ISensorDevice> sensor_device_;
    std::unique_ptr<caldera::backend::hal::MockSensorDevice> mock_device_;
    
    // Playback file path (for playback mode)
    std::string playback_file_;
    
    // Callbacks for frame data
    DepthFrameCallback depth_callback_;
    ColorFrameCallback color_callback_;
    
    // Recording support
    std::unique_ptr<caldera::backend::hal::SensorRecorder> recorder_;
    
    // Helper to get current device interface
    caldera::backend::hal::ISensorDevice* getCurrentDevice() const;
};

} // namespace caldera::backend::tools