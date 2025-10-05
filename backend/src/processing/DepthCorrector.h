#ifndef CALDERA_BACKEND_PROCESSING_DEPTHCORRECTOR_H
#define CALDERA_BACKEND_PROCESSING_DEPTHCORRECTOR_H

#include <string>
#include <vector>
#include <memory>
#include "common/Logger.h"
#include "common/DataTypes.h"
#include "processing/ProcessingTypes.h"
#include "tools/calibration/CalibrationTypes.h"

// Forward declaration
namespace spdlog { class logger; }

namespace caldera::backend::processing {

/**
 * @brief Per-pixel depth correction for lens distortion compensation
 * 
 * Corrects depth values using calibration data to compensate for:
 * - Lens optical distortions
 * - Per-pixel depth variations  
 * - Systematic sensor errors
 * 
 * Based on SARndbox PixelDepthCorrection approach
 */
class DepthCorrector {
public:


    /**
     * @brief Constructor
     */
    DepthCorrector();

    /**
     * @brief Load correction profile for sensor
     * @param sensorId Sensor identifier
     * @return true if profile loaded successfully
     */
    bool loadProfile(const std::string& sensorId);

    /**
     * @brief Check if corrector has valid profile loaded
     */
    bool isReady() const { return profile_.isValid; }

    /**
     * @brief Get loaded sensor ID
     */
    const std::string& getSensorId() const { return profile_.sensorId; }

    /**
     * @brief Correct single depth value at specific pixel
     * @param x Pixel x coordinate
     * @param y Pixel y coordinate  
     * @param rawDepth Raw depth value from sensor
     * @return Corrected depth value
     */
    float correctPixel(int x, int y, float rawDepth) const;

    /**
     * @brief Correct entire depth frame in-place
     * @param frame Input/output depth frame
     */
    void correctFrame(common::RawDepthFrame& frame) const;

    /**
     * @brief Create correction profile from calibration data
     * @param sensorId Sensor to create profile for
     * @param calibrationProfile Calibration data source
     * @return Generated correction profile
     */
    static CorrectionProfile createProfile(
        const std::string& sensorId,
        const tools::calibration::SensorCalibrationProfile& calibrationProfile
    );

private:
    std::shared_ptr<spdlog::logger> logger_;
    CorrectionProfile profile_;

    /**
     * @brief Get correction factor for pixel
     */
    float getCorrectionFactor(int x, int y) const;

    /**
     * @brief Validate pixel coordinates
     */
    bool isValidPixel(int x, int y) const;
};

} // namespace caldera::backend::processing

#endif // CALDERA_BACKEND_PROCESSING_DEPTHCORRECTOR_H