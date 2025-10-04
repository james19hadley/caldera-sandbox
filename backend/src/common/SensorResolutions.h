#pragma once

namespace caldera::backend::common {

/**
 * Centralized sensor resolution constants.
 * 
 * This header provides standardized resolution constants for all sensor types
 * and transport layer limits. Use these constants instead of magic numbers
 * throughout the codebase.
 */

// =============================================================================
// KINECT V1 RESOLUTIONS
// =============================================================================

namespace KinectV1 {
    // Kinect v1 operates at fixed VGA resolution for both depth and color
    static constexpr int WIDTH = 640;
    static constexpr int HEIGHT = 480;
    static constexpr int PIXEL_COUNT = WIDTH * HEIGHT;
    
    // Depth stream: 16-bit depth values
    static constexpr int DEPTH_BYTES_PER_PIXEL = 2;
    static constexpr int DEPTH_FRAME_SIZE_BYTES = PIXEL_COUNT * DEPTH_BYTES_PER_PIXEL;
    
    // Color stream: RGB format (3 bytes per pixel)
    static constexpr int COLOR_BYTES_PER_PIXEL = 3;
    static constexpr int COLOR_FRAME_SIZE_BYTES = PIXEL_COUNT * COLOR_BYTES_PER_PIXEL;
}

// =============================================================================
// KINECT V2 TYPICAL RESOLUTIONS
// =============================================================================

namespace KinectV2 {
    // Kinect v2 depth stream typical resolution
    static constexpr int DEPTH_WIDTH = 512;
    static constexpr int DEPTH_HEIGHT = 424;
    static constexpr int DEPTH_PIXEL_COUNT = DEPTH_WIDTH * DEPTH_HEIGHT;
    
    // Kinect v2 color stream typical resolution 
    static constexpr int COLOR_WIDTH = 1920;
    static constexpr int COLOR_HEIGHT = 1080; 
    static constexpr int COLOR_PIXEL_COUNT = COLOR_WIDTH * COLOR_HEIGHT;
    
    // Color stream: BGRX format (4 bytes per pixel)
    static constexpr int COLOR_BYTES_PER_PIXEL = 4;
    static constexpr int COLOR_FRAME_SIZE_BYTES = COLOR_PIXEL_COUNT * COLOR_BYTES_PER_PIXEL;
}

// =============================================================================
// HIGH RESOLUTION AND FUTURE SENSOR SUPPORT
// =============================================================================

namespace HighRes {
    // 2K resolution support for future sensors
    static constexpr int WIDTH_2K = 2560;
    static constexpr int HEIGHT_2K = 1440;
    
    // 4K resolution support for future sensors  
    static constexpr int WIDTH_4K = 3840;
    static constexpr int HEIGHT_4K = 2160;
    
    // Ultra-wide support
    static constexpr int WIDTH_ULTRAWIDE = 3440;
    static constexpr int HEIGHT_ULTRAWIDE = 1440;
}

// =============================================================================
// MULTI-SENSOR FUSION CONFIGURATIONS
// =============================================================================

namespace MultiSensor {
    // Dual Kinect v1 side-by-side fusion (example: 2×640 = 1280 width)
    static constexpr int DUAL_KINECT_V1_WIDTH = KinectV1::WIDTH * 2;    // 1280
    static constexpr int DUAL_KINECT_V1_HEIGHT = KinectV1::HEIGHT;      // 480
    
    // Dual Kinect v2 depth fusion (example: 2×512 = 1024 width) 
    static constexpr int DUAL_KINECT_V2_DEPTH_WIDTH = KinectV2::DEPTH_WIDTH * 2;  // 1024
    static constexpr int DUAL_KINECT_V2_DEPTH_HEIGHT = KinectV2::DEPTH_HEIGHT;    // 424
    
    // Mixed sensors: Kinect v1 + v2 (worst case: max dimensions)
    static constexpr int MIXED_V1_V2_WIDTH = std::max(KinectV1::WIDTH, KinectV2::COLOR_WIDTH);        // 1920
    static constexpr int MIXED_V1_V2_HEIGHT = std::max(KinectV1::HEIGHT, KinectV2::COLOR_HEIGHT);     // 1080
    
    // Large multi-sensor array (4+ sensors in grid)
    static constexpr int MULTI_ARRAY_WIDTH = HighRes::WIDTH_2K;     // 2560 
    static constexpr int MULTI_ARRAY_HEIGHT = HighRes::HEIGHT_2K;   // 1440
    
    // Processing layer can create custom fused frames larger than any single sensor
    static constexpr int FUSED_FRAME_MAX_WIDTH = HighRes::WIDTH_4K;   // 3840
    static constexpr int FUSED_FRAME_MAX_HEIGHT = HighRes::HEIGHT_4K; // 2160
}

// =============================================================================
// TRANSPORT LAYER LIMITS  
// =============================================================================

namespace Transport {
    // SharedMemory transport capacity limits
    // Single sensor scenarios
    static constexpr int SHM_SINGLE_SENSOR_WIDTH = KinectV2::COLOR_WIDTH;   // 1920
    static constexpr int SHM_SINGLE_SENSOR_HEIGHT = KinectV2::COLOR_HEIGHT; // 1080
    
    // Multi-sensor scenarios (processing layer fusion)
    static constexpr int SHM_MULTI_SENSOR_WIDTH = MultiSensor::MULTI_ARRAY_WIDTH;    // 2560
    static constexpr int SHM_MULTI_SENSOR_HEIGHT = MultiSensor::MULTI_ARRAY_HEIGHT;  // 1440
    
    // Maximum capacity for future expansion (processing can create any size)
    static constexpr int SHM_HARD_MAX_WIDTH = MultiSensor::FUSED_FRAME_MAX_WIDTH;   // 3840
    static constexpr int SHM_HARD_MAX_HEIGHT = MultiSensor::FUSED_FRAME_MAX_HEIGHT; // 2160
    
    // Legacy compatibility
    static constexpr int SHM_LEGACY_MAX_WIDTH = KinectV1::WIDTH;   // 640
    static constexpr int SHM_LEGACY_MAX_HEIGHT = KinectV1::HEIGHT; // 480
    
    // Smart auto-detection based on sensor configuration and processing capabilities
    enum class SensorConfiguration {
        SINGLE_KINECT_V1,      // 640x480
        SINGLE_KINECT_V2,      // 1920x1080 (color stream)  
        DUAL_SENSOR,           // 2560x1440 (side-by-side or fused)
        MULTI_SENSOR_ARRAY,    // 2560x1440 (grid/array)
        PROCESSING_FUSION,     // Up to 3840x2160 (processing layer creates large fused frames)
        LEGACY_SMALL           // 640x480 (synthetic/test)
    };
    
    static constexpr std::pair<int, int> getOptimalSize(SensorConfiguration config) {
        switch (config) {
            case SensorConfiguration::SINGLE_KINECT_V1:
                return {KinectV1::WIDTH, KinectV1::HEIGHT};
            case SensorConfiguration::SINGLE_KINECT_V2:
                return {SHM_SINGLE_SENSOR_WIDTH, SHM_SINGLE_SENSOR_HEIGHT};
            case SensorConfiguration::DUAL_SENSOR:
                return {MultiSensor::DUAL_KINECT_V2_DEPTH_WIDTH, MultiSensor::DUAL_KINECT_V2_DEPTH_HEIGHT};
            case SensorConfiguration::MULTI_SENSOR_ARRAY:
            case SensorConfiguration::PROCESSING_FUSION:
                return {SHM_MULTI_SENSOR_WIDTH, SHM_MULTI_SENSOR_HEIGHT};
            case SensorConfiguration::LEGACY_SMALL:
                return {SHM_LEGACY_MAX_WIDTH, SHM_LEGACY_MAX_HEIGHT};
            default:
                return {SHM_SINGLE_SENSOR_WIDTH, SHM_SINGLE_SENSOR_HEIGHT};
        }
    }
    
    // Legacy helper functions (kept for backwards compatibility)
    static constexpr int getRecommendedWidth(bool high_res_mode = false, bool multi_sensor = false) {
        if (multi_sensor) return SHM_MULTI_SENSOR_WIDTH;
        if (high_res_mode) return SHM_MULTI_SENSOR_WIDTH;
        return SHM_SINGLE_SENSOR_WIDTH;
    }
    
    static constexpr int getRecommendedHeight(bool high_res_mode = false, bool multi_sensor = false) {
        if (multi_sensor) return SHM_MULTI_SENSOR_HEIGHT;
        if (high_res_mode) return SHM_MULTI_SENSOR_HEIGHT;
        return SHM_SINGLE_SENSOR_HEIGHT;
    }
}

// =============================================================================
// GUI DISPLAY RESOLUTIONS
// =============================================================================

namespace Display {
    // Kinect v1 appropriate window sizes
    static constexpr int KINECT_V1_DEPTH_WINDOW_WIDTH = KinectV1::WIDTH;   // 640
    static constexpr int KINECT_V1_DEPTH_WINDOW_HEIGHT = KinectV1::HEIGHT; // 480
    static constexpr int KINECT_V1_COLOR_WINDOW_WIDTH = KinectV1::WIDTH;   // 640  
    static constexpr int KINECT_V1_COLOR_WINDOW_HEIGHT = KinectV1::HEIGHT; // 480
    
    // Kinect v2 appropriate window sizes (scaled down for display)
    static constexpr int KINECT_V2_DEPTH_WINDOW_WIDTH = KinectV1::WIDTH;   // 640 (scaled up from 512)
    static constexpr int KINECT_V2_DEPTH_WINDOW_HEIGHT = KinectV1::HEIGHT; // 480 (scaled up from 424)
    static constexpr int KINECT_V2_COLOR_WINDOW_WIDTH = 960;  // Scaled down from 1920
    static constexpr int KINECT_V2_COLOR_WINDOW_HEIGHT = 540; // Scaled down from 1080
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

namespace ResolutionUtils {
    
    /**
     * Get expected frame size for given dimensions and bytes per pixel
     */
    constexpr size_t getFrameSize(int width, int height, int bytes_per_pixel) {
        return static_cast<size_t>(width) * height * bytes_per_pixel;
    }
    
    /**
     * Get pixel count for given dimensions
     */
    constexpr size_t getPixelCount(int width, int height) {
        return static_cast<size_t>(width) * height;
    }
}

} // namespace caldera::backend::common