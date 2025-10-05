#include "SensorCalibration.h"
#include <iostream>
#include <string>
#include "spdlog/spdlog.h"

using namespace caldera::backend::tools::calibration;

void showHelp() {
    std::cout << "\nCaldera Sensor Calibration Tool\n";
    std::cout << "==============================\n\n";
    std::cout << "Usage: CalibrationTool <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  list-sensors            List available sensor types\n";
    std::cout << "  list-profiles           List saved calibration profiles\n";
    std::cout << "  calibrate <sensor-id>   Calibrate sensor (automatic)\n";
    std::cout << "  show <sensor-id>        Show calibration profile details\n";
    std::cout << "  validate <sensor-id>    Validate existing calibration\n";
    std::cout << "  delete <sensor-id>      Delete calibration profile\n\n";
    std::cout << "Options:\n";
    std::cout << "  --debug, -d             Enable debug logging\n\n";
    std::cout << "Sensor IDs:\n";
    std::cout << "  kinect-v1               Microsoft Kinect v1 (Xbox 360)\n";
    std::cout << "  kinect-v2               Microsoft Kinect v2 (Xbox One)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  CalibrationTool calibrate kinect-v1\n";
    std::cout << "  CalibrationTool calibrate kinect-v1 --debug\n";
    std::cout << "  CalibrationTool validate kinect-v1\n";
    std::cout << "  CalibrationTool show kinect-v1\n\n";
}

int main(int argc, char* argv[]) {
    // Initialize logger 
    caldera::backend::common::Logger::instance().initialize("logs/calibration.log", spdlog::level::info);
    
    // Check for debug flag
    bool debugMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug" || std::string(argv[i]) == "-d") {
            debugMode = true;
            caldera::backend::common::Logger::instance().setGlobalLevel(spdlog::level::debug);
            std::cout << "Debug logging enabled\n";
            break;
        }
    }
    
    if (argc < 2) {
        showHelp();
        return 1;
    }
    
    std::string command = argv[1];
    SensorCalibration calibrator;
    
    try {
        if (command == "help" || command == "-h" || command == "--help") {
            showHelp();
            return 0;
        }
        else if (command == "list-sensors") {
            std::cout << "\nAvailable Sensor Types:\n";
            std::cout << "======================\n";
            auto sensors = calibrator.getAvailableSensorTypes();
            for (const auto& sensor : sensors) {
                std::cout << "  " << sensor << std::endl;
            }
            std::cout << std::endl;
            return 0;
        }
        else if (command == "list-profiles") {
            std::cout << "\nSaved Calibration Profiles:\n";
            std::cout << "===========================\n";
            auto profiles = calibrator.listCalibrationProfiles();
            if (profiles.empty()) {
                std::cout << "  No calibration profiles found.\n";
                std::cout << "  Use 'CalibrationTool calibrate <sensor-id>' to create one.\n";
            } else {
                for (const auto& profile : profiles) {
                    std::cout << "  " << profile << std::endl;
                }
            }
            std::cout << std::endl;
            return 0;
        }
        else if (command == "calibrate") {
            if (argc < 3) {
                std::cerr << "Error: Missing sensor ID for calibration\n";
                std::cerr << "Usage: CalibrationTool calibrate <sensor-id>\n";
                return 1;
            }
            
            std::string sensorId = argv[2];
            std::cout << "\nStarting calibration for sensor: " << sensorId << std::endl;
            
            auto sensor = calibrator.createSensorDevice(sensorId);
            if (!sensor) {
                std::cerr << "Error: Unknown sensor ID: " << sensorId << std::endl;
                return 1;
            }
            
            std::cout << "Position a flat surface (table, floor, etc.) in sensor view.\n";
            std::cout << "Press ENTER when ready...";
            std::cin.get();
            
            CalibrationConfig config = calibrator.getDefaultConfig();
            PlaneCalibrationData result;
            
            CalibrationResult calibResult = calibrator.collectAutomaticCalibration(sensor, config, result);
            
            if (calibResult != CalibrationResult::Success) {
                std::cerr << "Calibration failed!" << std::endl;
                return 1;
            }
            
            // Create and save profile
            SensorCalibrationProfile profile;
            profile.sensorId = sensorId;
            profile.sensorType = sensorId;  // Simplified
            profile.basePlaneCalibration = result;
            
            auto now = std::chrono::system_clock::now();
            profile.createdAt = now;
            profile.lastUpdated = now;
            
            if (calibrator.saveCalibrationProfile(profile)) {
                std::cout << "\n✓ Calibration completed and saved successfully!\n";
                std::cout << "Points collected: " << result.collectedPoints.size() << std::endl;
                std::cout << "Average distance to plane: " << result.avgDistanceToPlane << " m\n";
                std::cout << "R² fit quality: " << result.planeFitRSquared << std::endl;
            } else {
                std::cerr << "✗ Failed to save calibration profile\n";
                return 1;
            }
            
            return 0;
        }
        else if (command == "validate") {
            if (argc < 3) {
                std::cerr << "Error: Missing sensor ID for validation\n";
                std::cerr << "Usage: CalibrationTool validate <sensor-id>\n";
                return 1;
            }
            
            std::string sensorId = argv[2];
            std::cout << "\nValidating calibration for sensor: " << sensorId << std::endl;
            
            if (!calibrator.hasCalibrationProfile(sensorId)) {
                std::cerr << "Error: No calibration profile found for sensor: " << sensorId << std::endl;
                std::cerr << "Run 'CalibrationTool calibrate " << sensorId << "' first\n";
                return 1;
            }
            
            std::cout << "Position the same calibration surface in view.\n";
            std::cout << "Press ENTER to start validation...";
            std::cin.get();
            
            float avgDistance = calibrator.validateCalibration(sensorId, 10);
            
            if (avgDistance < 0) {
                std::cerr << "✗ Validation failed\n";
                return 1;
            }
            
            std::cout << "\nValidation Results:\n";
            std::cout << "Average distance to calibrated plane: " << avgDistance << " m\n";
            
            bool passed = avgDistance < 0.02f;  // 2cm threshold
            std::cout << "Validation: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;
            
            return passed ? 0 : 1;
        }
        else if (command == "show") {
            if (argc < 3) {
                std::cerr << "Error: Missing sensor ID\n";
                std::cerr << "Usage: CalibrationTool show <sensor-id>\n";
                return 1;
            }
            
            std::string sensorId = argv[2];
            SensorCalibrationProfile profile;
            
            if (!calibrator.loadCalibrationProfile(sensorId, profile)) {
                std::cerr << "Error: No calibration profile found for sensor: " << sensorId << std::endl;
                return 1;
            }
            
            std::cout << "\nCalibration Profile: " << profile.sensorId << std::endl;
            std::cout << std::string(40, '=') << std::endl;
            std::cout << "Sensor Type: " << profile.sensorType << std::endl;
            std::cout << "Points collected: " << profile.basePlaneCalibration.collectedPoints.size() << std::endl;
            std::cout << "Average distance to plane: " << profile.basePlaneCalibration.avgDistanceToPlane << " m\n";
            std::cout << "R² fit quality: " << profile.basePlaneCalibration.planeFitRSquared << std::endl;
            std::cout << "Valid: " << (profile.basePlaneCalibration.isValidCalibration ? "✓ Yes" : "✗ No") << std::endl;
            std::cout << std::endl;
            
            return 0;
        }
        else if (command == "delete") {
            if (argc < 3) {
                std::cerr << "Error: Missing sensor ID\n";
                std::cerr << "Usage: CalibrationTool delete <sensor-id>\n";
                return 1;
            }
            
            std::string sensorId = argv[2];
            
            if (!calibrator.hasCalibrationProfile(sensorId)) {
                std::cerr << "Error: No calibration profile found for sensor: " << sensorId << std::endl;
                return 1;
            }
            
            std::cout << "Delete calibration profile for sensor '" << sensorId << "'? (y/N): ";
            std::string response;
            std::getline(std::cin, response);
            
            if (response == "y" || response == "Y" || response == "yes") {
                if (calibrator.deleteCalibrationProfile(sensorId)) {
                    std::cout << "✓ Calibration profile deleted: " << sensorId << std::endl;
                } else {
                    std::cerr << "✗ Failed to delete calibration profile: " << sensorId << std::endl;
                    return 1;
                }
            } else {
                std::cout << "Operation cancelled.\n";
            }
            
            return 0;
        }
        else {
            std::cerr << "Error: Unknown command '" << command << "'\n";
            showHelp();
            return 1;
        }
        
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}