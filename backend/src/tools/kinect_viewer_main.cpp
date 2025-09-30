#include "tools/KinectDataViewer.h"
#include "tools/SimpleViewer.h"
#include "tools/VideoWindow.h"
#include "common/Logger.h"
#include <iostream>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>

using caldera::backend::tools::KinectDataViewer;
using caldera::backend::common::Logger;

enum class WindowMode {
    NONE,
    DEPTH_ASCII,
    DEPTH_STATS,
    COLOR_STATS,
    DEPTH_WINDOW,
    COLOR_WINDOW
};

std::atomic<bool> should_exit(false);

void signal_handler(int signal) {
    should_exit.store(true);
}

void show_usage(const char* program_name) {
    std::cout << "Caldera Backend - Universal Sensor Data Viewer" << std::endl;
    std::cout << "View data from Kinect V1, V2 and other supported sensors" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -t, --time SECONDS    Run for specified seconds (default: run until Ctrl+C)" << std::endl;
    std::cout << "  -s, --sensor TYPE     Sensor type: auto, v1, v2 (default: auto)" << std::endl;
    std::cout << "  -w, --window TYPE     Visual mode: depth-ascii, depth-stats, color-stats, depth, color" << std::endl;
    std::cout << "  -r, --record FILE     Record sensor data to file for testing" << std::endl;
    std::cout << "  -p, --playback FILE   Playback recorded sensor data" << std::endl;
    std::cout << "  --loop               Loop playback (only with --playback)" << std::endl;
    std::cout << "  --fps FPS            Playback frame rate (default: 30)" << std::endl;
    std::cout << "  -h, --help           Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                           # Auto-detect sensor (text output)" << std::endl;
    std::cout << "  " << program_name << " -s v2                   # Force Kinect V2" << std::endl;
    std::cout << "  " << program_name << " -w depth-ascii -t 10    # ASCII depth visualization for 10s" << std::endl;
    std::cout << "  " << program_name << " -w depth-stats          # Depth frame statistics" << std::endl;
    std::cout << "  " << program_name << " -w color-stats          # Color frame statistics" << std::endl;
    std::cout << "  " << program_name << " -w depth                # Real-time depth video window" << std::endl;
    std::cout << "  " << program_name << " -w color                # Real-time color video window" << std::endl;
    std::cout << "  " << program_name << " -r test_data.dat -t 30  # Record 30 seconds of data" << std::endl;
    std::cout << "  " << program_name << " -r data.dat -w depth    # Record while showing depth window" << std::endl;
    std::cout << "  " << program_name << " -p data.dat -w depth    # Playback recorded data with depth window" << std::endl;
    std::cout << "  " << program_name << " -p data.dat --loop --fps 60  # Loop playback at 60 FPS" << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int run_time = 0; // 0 = run until stopped
    caldera::backend::tools::SensorType sensor_type = caldera::backend::tools::SensorType::AUTO_DETECT;
    WindowMode window_mode = WindowMode::NONE;
    std::string record_file = ""; // Empty = no recording
    std::string playback_file = ""; // Empty = no playback
    bool playback_loop = false;
    double playback_fps = 30.0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            show_usage(argv[0]);
            return 0;
        } else if (arg == "-t" || arg == "--time") {
            if (i + 1 < argc) {
                try {
                    run_time = std::stoi(argv[++i]);
                } catch (const std::exception&) {
                    std::cerr << "Error: Invalid time value: " << argv[i] << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --time requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "-s" || arg == "--sensor") {
            if (i + 1 < argc) {
                std::string sensor_str = argv[++i];
                if (sensor_str == "auto") {
                    sensor_type = caldera::backend::tools::SensorType::AUTO_DETECT;
                } else if (sensor_str == "v1") {
                    sensor_type = caldera::backend::tools::SensorType::KINECT_V1;
                } else if (sensor_str == "v2") {
                    sensor_type = caldera::backend::tools::SensorType::KINECT_V2;
                } else {
                    std::cerr << "Error: Invalid sensor type: " << sensor_str << std::endl;
                    std::cerr << "Valid types: auto, v1, v2" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --sensor requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "-w" || arg == "--window") {
            if (i + 1 < argc) {
                std::string window_str = argv[++i];
                if (window_str == "depth-ascii") {
                    window_mode = WindowMode::DEPTH_ASCII;
                } else if (window_str == "depth-stats") {
                    window_mode = WindowMode::DEPTH_STATS;
                } else if (window_str == "color-stats") {
                    window_mode = WindowMode::COLOR_STATS;
                } else if (window_str == "depth") {
                    window_mode = WindowMode::DEPTH_WINDOW;
                } else if (window_str == "color") {
                    window_mode = WindowMode::COLOR_WINDOW;
                } else {
                    std::cerr << "Error: Invalid window type: " << window_str << std::endl;
                    std::cerr << "Valid types: depth-ascii, depth-stats, color-stats, depth, color" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --window requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "-r" || arg == "--record") {
            if (i + 1 < argc) {
                record_file = argv[++i];
            } else {
                std::cerr << "Error: --record requires a filename" << std::endl;
                return 1;
            }
        } else if (arg == "-p" || arg == "--playback") {
            if (i + 1 < argc) {
                playback_file = argv[++i];
            } else {
                std::cerr << "Error: --playback requires a filename" << std::endl;
                return 1;
            }
        } else if (arg == "--loop") {
            playback_loop = true;
        } else if (arg == "--fps") {
            if (i + 1 < argc) {
                try {
                    playback_fps = std::stod(argv[++i]);
                } catch (const std::exception&) {
                    std::cerr << "Error: Invalid fps value: " << argv[i] << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --fps requires a value" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            show_usage(argv[0]);
            return 1;
        }
    }



    // Validate arguments
    if (!record_file.empty() && !playback_file.empty()) {
        std::cerr << "Error: Cannot use --record and --playback together" << std::endl;
        return 1;
    }

    // Initialize logger
    Logger::instance().initialize("logs/sensor_viewer.log", spdlog::level::info);

    // Create viewers based on window mode
    std::unique_ptr<caldera::backend::tools::SimpleViewer> simple_viewer;
    std::unique_ptr<caldera::backend::tools::VideoWindow> video_window;
    
    if (window_mode == WindowMode::DEPTH_ASCII || window_mode == WindowMode::DEPTH_STATS || window_mode == WindowMode::COLOR_STATS) {
        simple_viewer = std::make_unique<caldera::backend::tools::SimpleViewer>("Kinect Data Viewer");
    } else if (window_mode == WindowMode::DEPTH_WINDOW) {
        video_window = std::make_unique<caldera::backend::tools::VideoWindow>("Kinect Depth Stream", 640, 480);
    } else if (window_mode == WindowMode::COLOR_WINDOW) {
        video_window = std::make_unique<caldera::backend::tools::VideoWindow>("Kinect Color Stream", 960, 540);
    }

    // Create appropriate viewer
    std::unique_ptr<KinectDataViewer> viewer;
    if (!playback_file.empty()) {
        viewer = std::make_unique<KinectDataViewer>(playback_file, caldera::backend::tools::ViewMode::TEXT_ONLY);
        viewer->setPlaybackOptions(playback_loop, playback_fps);
    } else {
        viewer = std::make_unique<KinectDataViewer>(sensor_type, caldera::backend::tools::ViewMode::TEXT_ONLY);
    }

    // Set up callbacks for visual display
    if (simple_viewer) {
        if (window_mode == WindowMode::DEPTH_ASCII) {
            viewer->setDepthFrameCallback([&simple_viewer](const caldera::backend::common::RawDepthFrame& frame) {
                simple_viewer->showDepthASCII(frame);
            });
        } else if (window_mode == WindowMode::DEPTH_STATS) {
            viewer->setDepthFrameCallback([&simple_viewer](const caldera::backend::common::RawDepthFrame& frame) {
                simple_viewer->showDepthFrame(frame);
            });
        } else if (window_mode == WindowMode::COLOR_STATS) {
            viewer->setColorFrameCallback([&simple_viewer](const caldera::backend::common::RawColorFrame& frame) {
                simple_viewer->showColorFrame(frame);
            });
        }
    }
    
    if (video_window) {
        if (window_mode == WindowMode::DEPTH_WINDOW) {
            viewer->setDepthFrameCallback([&video_window](const caldera::backend::common::RawDepthFrame& frame) {
                video_window->showDepthFrame(frame);
            });
        } else if (window_mode == WindowMode::COLOR_WINDOW) {
            viewer->setColorFrameCallback([&video_window](const caldera::backend::common::RawColorFrame& frame) {
                video_window->showColorFrame(frame);
            });
        }
    }

    if (!viewer->start()) {
        std::cerr << "Failed to start Kinect viewer" << std::endl;
        std::cerr << "Make sure:" << std::endl;
        std::cerr << "  1. Kinect V2 is connected to USB 3.0 port" << std::endl;
        std::cerr << "  2. Run: ./sensor_setup.sh setup" << std::endl;
        std::cerr << "  3. Check: ./sensor_setup.sh test" << std::endl;
        return 1;
    }

    // Start recording if requested
    if (!record_file.empty()) {
        if (!viewer->startRecording(record_file)) {
            std::cerr << "Failed to start recording to: " << record_file << std::endl;
            return 1;
        }
    }

    // Run the viewer
    try {
        if (run_time > 0) {
            viewer->runFor(run_time);
        } else {
            while (viewer->isRunning() && !should_exit.load()) {
                // Handle video window events
                if (video_window) {
                    video_window->pollEvents();
                    if (video_window->shouldClose()) {
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
            }
        }
        
        // Stop recording before stopping viewer
        if (viewer->isRecording()) {
            viewer->stopRecording();
        }
        
        // Explicitly stop viewer before cleanup
        viewer->stop();
        
        // Clean up video window first (while OpenGL context might still be valid)
        if (video_window) {
            video_window.reset();
        }
        
        // Then clean up viewer (this will be automatic via unique_ptr)
        viewer.reset();
        
        // Cleanup loggers to avoid crashes during shutdown
        spdlog::shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "Error during program execution: " << e.what() << std::endl;
        spdlog::shutdown();
        return 1;
    } catch (...) {
        std::cerr << "Unknown error during program execution" << std::endl;
        spdlog::shutdown();
        return 1;
    }

    return 0;
}