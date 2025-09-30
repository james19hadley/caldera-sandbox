#pragma once

#include "common/DataTypes.h"
#include <string>

namespace caldera::backend::tools {

/**
 * @brief Simple text-based viewer for depth and color data
 * 
 * Shows frame statistics and basic data analysis without external dependencies
 */
class SimpleViewer {
public:
    SimpleViewer(const std::string& name = "SimpleViewer");
    ~SimpleViewer() = default;

    /**
     * @brief Display depth frame statistics
     */
    void showDepthFrame(const caldera::backend::common::RawDepthFrame& frame);

    /**
     * @brief Display color frame statistics  
     */
    void showColorFrame(const caldera::backend::common::RawColorFrame& frame);

    /**
     * @brief Show a simple ASCII representation of depth data
     * @param frame Depth frame to visualize
     * @param rows Number of rows to show (default 20)
     * @param cols Number of columns to show (default 40)
     */
    void showDepthASCII(const caldera::backend::common::RawDepthFrame& frame, 
                        int rows = 20, int cols = 40);

private:
    std::string name_;
    uint32_t frame_count_;
};

} // namespace caldera::backend::tools