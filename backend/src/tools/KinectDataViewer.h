#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <functional>

namespace caldera::backend::hal {
    class KinectV2_Device;
}

namespace caldera::backend::common {
    struct RawDepthFrame;
    struct RawColorFrame;
}

namespace caldera::backend::tools {

enum class SensorType {
    KINECT_V2,
    KINECT_V1,  // Future support
    AUTO_DETECT
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
class KinectDataViewer {
public:
    explicit KinectDataViewer(SensorType type = SensorType::AUTO_DETECT, ViewMode mode = ViewMode::TEXT_ONLY);
    ~KinectDataViewer();

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

private:
    void viewerLoop();
    void printFrame(const std::string& type, size_t width, size_t height, 
                   size_t dataSize, uint64_t timestamp);

    SensorType sensorType_;
    ViewMode viewMode_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> viewer_thread_;
    caldera::backend::hal::KinectV2_Device* device_;  // TODO: Make polymorphic when V1 added
    
    // Callbacks for frame data
    DepthFrameCallback depth_callback_;
    ColorFrameCallback color_callback_;
};

} // namespace caldera::backend::tools