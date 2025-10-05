#include "tools/viewer/SimpleViewer.h"
#include "common/Logger.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace caldera::backend::tools {

using namespace caldera::backend::common;

SimpleViewer::SimpleViewer(const std::string& name) 
    : name_(name), frame_count_(0) {
    std::cout << "=== " << name_ << " Initialized ===" << std::endl;
}

void SimpleViewer::showDepthFrame(const RawDepthFrame& frame) {
    frame_count_++;
    
    std::cout << "\n--- DEPTH FRAME #" << frame_count_ << " ---" << std::endl;
    std::cout << "Resolution: " << frame.width << "x" << frame.height << std::endl;
    std::cout << "Timestamp: " << frame.timestamp_ns << " ns" << std::endl;
    
    if (!frame.data.empty()) {
        const uint16_t* depth_data = reinterpret_cast<const uint16_t*>(frame.data.data());
        size_t pixel_count = frame.width * frame.height;
        
        // Calculate basic statistics
        uint16_t min_depth = 65535, max_depth = 0;
        uint64_t sum = 0;
        uint32_t valid_pixels = 0;
        
        for (size_t i = 0; i < pixel_count; ++i) {
            uint16_t depth = depth_data[i];
            if (depth > 0) { // Valid depth reading
                min_depth = std::min(min_depth, depth);
                max_depth = std::max(max_depth, depth);
                sum += depth;
                valid_pixels++;
            }
        }
        
        std::cout << "Valid pixels: " << valid_pixels << "/" << pixel_count 
                  << " (" << (100.0 * valid_pixels / pixel_count) << "%)" << std::endl;
        
        if (valid_pixels > 0) {
            double avg_depth = static_cast<double>(sum) / valid_pixels;
            std::cout << "Depth range: " << min_depth << "mm - " << max_depth << "mm" << std::endl;
            std::cout << "Average depth: " << std::fixed << std::setprecision(1) 
                      << avg_depth << "mm" << std::endl;
        }
    }
    std::cout << std::endl;
}

void SimpleViewer::showColorFrame(const RawColorFrame& frame) {
    frame_count_++;
    
    std::cout << "\n--- COLOR FRAME #" << frame_count_ << " ---" << std::endl;
    std::cout << "Resolution: " << frame.width << "x" << frame.height << std::endl;
    std::cout << "Timestamp: " << frame.timestamp_ns << " ns" << std::endl;
    std::cout << "Data size: " << frame.data.size() << " bytes" << std::endl;
    
    if (!frame.data.empty() && frame.data.size() >= 12) {
        // Show first few pixel values as RGB
        const uint8_t* rgb_data = frame.data.data();
        std::cout << "First pixels (BGRX format): ";
        for (int i = 0; i < std::min(3, (int)frame.data.size() / 4); ++i) {
            int offset = i * 4;
            std::cout << "(" 
                      << (int)rgb_data[offset + 2] << "," // R
                      << (int)rgb_data[offset + 1] << "," // G 
                      << (int)rgb_data[offset + 0] << ") "; // B
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

void SimpleViewer::showDepthASCII(const RawDepthFrame& frame, int rows, int cols) {
    if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
        std::cout << "No depth data available" << std::endl;
        return;
    }
    
    const uint16_t* depth_data = reinterpret_cast<const uint16_t*>(frame.data.data());
    
    // Calculate sampling step
    int step_x = frame.width / cols;
    int step_y = frame.height / rows;
    
    std::cout << "\n--- DEPTH VISUALIZATION (" << cols << "x" << rows << ") ---" << std::endl;
    std::cout << "Sampling every " << step_x << "x" << step_y << " pixels" << std::endl;
    
    // Find min/max for normalization
    uint16_t min_depth = 65535, max_depth = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            int src_x = x * step_x;
            int src_y = y * step_y;
            if (src_x < (int)frame.width && src_y < (int)frame.height) {
                uint16_t depth = depth_data[src_y * frame.width + src_x];
                if (depth > 0) {
                    min_depth = std::min(min_depth, depth);
                    max_depth = std::max(max_depth, depth);
                }
            }
        }
    }
    
    if (min_depth == 65535) {
        std::cout << "No valid depth data in sampled area" << std::endl;
        return;
    }
    
    std::cout << "Depth range: " << min_depth << "mm - " << max_depth << "mm" << std::endl;
    
    // ASCII characters from closest to farthest
    const char* chars = " .:-=+*#%@";
    int char_count = strlen(chars);
    
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            int src_x = x * step_x;
            int src_y = y * step_y;
            
            if (src_x < (int)frame.width && src_y < (int)frame.height) {
                uint16_t depth = depth_data[src_y * frame.width + src_x];
                
                if (depth == 0) {
                    std::cout << " "; // No reading
                } else {
                    // Normalize depth to character index
                    double normalized = static_cast<double>(depth - min_depth) / (max_depth - min_depth);
                    int char_idx = static_cast<int>(normalized * (char_count - 1));
                    char_idx = std::max(0, std::min(char_count - 1, char_idx));
                    std::cout << chars[char_idx];
                }
            } else {
                std::cout << " ";
            }
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

} // namespace caldera::backend::tools