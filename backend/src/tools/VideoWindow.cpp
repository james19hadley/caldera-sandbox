#include "tools/VideoWindow.h"
#include "common/Logger.h"
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <algorithm>

namespace caldera::backend::tools {

bool VideoWindow::glfw_initialized_ = false;
int VideoWindow::window_count_ = 0;

VideoWindow::VideoWindow(const std::string& title, int width, int height) 
    : window_(nullptr), title_(title), width_(width), height_(height), texture_id_(0) {
    
    // Get logger for video window operations
    auto logger = common::Logger::instance().get("Tools.VideoWindow");
    
    // Initialize GLFW if this is the first window
    if (!glfw_initialized_) {
        if (!glfwInit()) {
            logger->error("Failed to initialize GLFW");
            return;
        }
        glfw_initialized_ = true;
    }
    
    // Create window but don't make context current yet
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Start invisible
    window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
    
    if (!window_) {
        logger->error("Failed to create GLFW window: " + title_);
        return;
    }
    
    // Don't make context current during initialization
    
    window_count_++;
    logger->debug("Created video window: " + title_ + " (" + 
                  std::to_string(width_) + "x" + std::to_string(height_) + ")");
}

VideoWindow::~VideoWindow() noexcept {
    try {
        cleanup();
    } catch (...) {
        // Ignore OpenGL cleanup errors during destruction
    }
    
    window_count_--;
    
    // Cleanup GLFW if this was the last window
    try {
        if (window_count_ == 0 && glfw_initialized_) {
            glfwTerminate();
            glfw_initialized_ = false;
        }
    } catch (...) {
        // Ignore GLFW cleanup errors during destruction
    }
}

bool VideoWindow::initializeGL() {
    if (!window_) return false;
    
    // Delay OpenGL initialization until first frame
    return true;
}

void VideoWindow::cleanup() {
    // Clean up OpenGL resources if context is still valid
    try {
        if (window_ && texture_id_) {
            glfwMakeContextCurrent(window_);
            glDeleteTextures(1, &texture_id_);
            texture_id_ = 0;
        }
    } catch (...) {
        // Ignore OpenGL errors during cleanup
        texture_id_ = 0;
    }
    
    // Clean up GLFW window
    try {
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
    } catch (...) {
        // Ignore GLFW errors during cleanup
        window_ = nullptr;
    }
}

bool VideoWindow::showDepthFrame(const caldera::backend::common::RawDepthFrame& frame) {
    if (!window_ || frame.data.empty()) return false;
    
    glfwMakeContextCurrent(window_);
    
    // Lazy OpenGL initialization on first frame
    if (texture_id_ == 0) {
        // Make window visible and setup OpenGL
        glfwShowWindow(window_);
        glfwSwapInterval(1); // Enable vsync
        
        // Set up OpenGL state
        glViewport(0, 0, width_, height_);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width_, height_, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        
        // Create texture
        glGenTextures(1, &texture_id_);
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
    
    // Convert depth data to grayscale image
    const uint16_t* depth_data = reinterpret_cast<const uint16_t*>(frame.data.data());
    std::vector<uint8_t> grayscale_data(frame.width * frame.height);
    
    // Find min/max for normalization
    uint16_t min_depth = 65535, max_depth = 0;
    for (size_t i = 0; i < frame.width * frame.height; ++i) {
        if (depth_data[i] > 0) {
            min_depth = std::min(min_depth, depth_data[i]);
            max_depth = std::max(max_depth, depth_data[i]);
        }
    }
    
    // Convert to 8-bit grayscale
    for (size_t i = 0; i < frame.width * frame.height; ++i) {
        if (depth_data[i] == 0) {
            grayscale_data[i] = 0; // Invalid pixels = black
        } else if (max_depth > min_depth) {
            float normalized = static_cast<float>(depth_data[i] - min_depth) / (max_depth - min_depth);
            grayscale_data[i] = static_cast<uint8_t>(255 * (1.0f - normalized)); // Closer = brighter
        } else {
            grayscale_data[i] = 128;
        }
    }
    
    // Upload to OpenGL texture
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame.width, frame.height, 0, 
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, grayscale_data.data());
    
    // Draw texture to screen
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    
    float tex_width = static_cast<float>(frame.width);
    float tex_height = static_cast<float>(frame.height);
    
    // Scale to fit window while maintaining aspect ratio
    float scale_x = static_cast<float>(width_) / tex_width;
    float scale_y = static_cast<float>(height_) / tex_height;
    float scale = std::min(scale_x, scale_y);
    
    float render_width = tex_width * scale;
    float render_height = tex_height * scale;
    float x_offset = (width_ - render_width) / 2.0f;
    float y_offset = (height_ - render_height) / 2.0f;
    
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x_offset, y_offset);
    glTexCoord2f(1, 0); glVertex2f(x_offset + render_width, y_offset);
    glTexCoord2f(1, 1); glVertex2f(x_offset + render_width, y_offset + render_height);
    glTexCoord2f(0, 1); glVertex2f(x_offset, y_offset + render_height);
    glEnd();
    
    glfwSwapBuffers(window_);
    return true;
}

bool VideoWindow::showColorFrame(const caldera::backend::common::RawColorFrame& frame) {
    if (!window_ || frame.data.empty()) return false;
    
    glfwMakeContextCurrent(window_);
    
    // Lazy OpenGL initialization on first frame
    if (texture_id_ == 0) {
        // Make window visible and setup OpenGL
        glfwShowWindow(window_);
        glfwSwapInterval(1); // Enable vsync
        
        // Set up OpenGL state
        glViewport(0, 0, width_, height_);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width_, height_, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        
        // Create texture
        glGenTextures(1, &texture_id_);
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
    
    // Convert BGRX to RGB
    std::vector<uint8_t> rgb_data(frame.width * frame.height * 3);
    const uint8_t* bgrx_data = frame.data.data();
    
    for (size_t i = 0; i < frame.width * frame.height; ++i) {
        rgb_data[i * 3 + 0] = bgrx_data[i * 4 + 2]; // R = B from BGRX
        rgb_data[i * 3 + 1] = bgrx_data[i * 4 + 1]; // G = G from BGRX  
        rgb_data[i * 3 + 2] = bgrx_data[i * 4 + 0]; // B = R from BGRX
    }
    
    // Upload to OpenGL texture
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame.width, frame.height, 0, 
                 GL_RGB, GL_UNSIGNED_BYTE, rgb_data.data());
    
    // Draw texture to screen (same as depth)
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    
    float tex_width = static_cast<float>(frame.width);
    float tex_height = static_cast<float>(frame.height);
    
    // Scale to fit window while maintaining aspect ratio
    float scale_x = static_cast<float>(width_) / tex_width;
    float scale_y = static_cast<float>(height_) / tex_height;
    float scale = std::min(scale_x, scale_y);
    
    float render_width = tex_width * scale;
    float render_height = tex_height * scale;
    float x_offset = (width_ - render_width) / 2.0f;
    float y_offset = (height_ - render_height) / 2.0f;
    
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x_offset, y_offset);
    glTexCoord2f(1, 0); glVertex2f(x_offset + render_width, y_offset);
    glTexCoord2f(1, 1); glVertex2f(x_offset + render_width, y_offset + render_height);
    glTexCoord2f(0, 1); glVertex2f(x_offset, y_offset + render_height);
    glEnd();
    
    glfwSwapBuffers(window_);
    return true;
}

bool VideoWindow::shouldClose() const {
    return window_ ? glfwWindowShouldClose(window_) : true;
}

void VideoWindow::pollEvents() {
    glfwPollEvents();
}

} // namespace caldera::backend::tools