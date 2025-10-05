#pragma once

#include "common/DataTypes.h"
#include <string>
#include <memory>

struct GLFWwindow;

namespace caldera::backend::tools {

/**
 * @brief Simple OpenGL window for displaying depth/color video streams
 * 
 * Uses GLFW + OpenGL for real-time video display without heavy dependencies
 */
class VideoWindow {
public:
    VideoWindow(const std::string& title, int width, int height);
    ~VideoWindow() noexcept;

    /**
     * @brief Display a depth frame as grayscale image
     */
    bool showDepthFrame(const caldera::backend::common::RawDepthFrame& frame);

    /**
     * @brief Display a color frame as RGB image  
     */
    bool showColorFrame(const caldera::backend::common::RawColorFrame& frame);

    /**
     * @brief Check if window should close
     */
    bool shouldClose() const;

    /**
     * @brief Process window events (call regularly)
     */
    void pollEvents();

    /**
     * @brief Force the window to become visible immediately (useful if first frame is delayed)
     */
    void show();

private:
    GLFWwindow* window_;
    std::string title_;
    int width_, height_;
    
    // OpenGL texture for displaying images
    unsigned int texture_id_;
    
    bool initializeGL();
    void cleanup();
    static bool glfw_initialized_;
    static int window_count_;
};

} // namespace caldera::backend::tools