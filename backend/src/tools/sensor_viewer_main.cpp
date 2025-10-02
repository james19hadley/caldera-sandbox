#include "tools/SensorViewerCore.h"
#include "tools/SimpleViewer.h"
#include "tools/VideoWindow.h"
#include "tools/SensorEnumerator.h"
#include "common/Logger.h"
#include <iostream>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <mutex>
#include <optional>

using caldera::backend::tools::SensorViewerCore;
using caldera::backend::common::Logger;

enum class ViewOutputMode {
    TEXT,
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
    std::cout << "Caldera Backend - Sensor Viewer" << std::endl;
    std::cout << "View data from Kinect V1, V2 and future sensors" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -t, --time SECONDS    Run for specified seconds (default: run until Ctrl+C)" << std::endl;
    std::cout << "  -s, --sensor TYPE     Sensor type: auto, v1, v2 (default: auto). If auto and multiple available you'll be prompted." << std::endl;
    std::cout << "  -l, --list            List detected sensors and exit" << std::endl;
    std::cout << "  -m, --mode MODE       Output mode: text, ascii, depth-stats, color-stats, depth-window, color-window (alias: window=depth-window)" << std::endl;
    std::cout << "  -w, --window TYPE     (Deprecated alias of --mode for backward compatibility)" << std::endl;
    std::cout << "  -r, --record FILE     Record sensor data to file for testing" << std::endl;
    std::cout << "  -p, --playback FILE   Playback recorded sensor data" << std::endl;
    std::cout << "  --loop               Loop playback (only with --playback)" << std::endl;
    std::cout << "  --fps FPS            Playback frame rate (default: 30)" << std::endl;
    std::cout << "  --headless           Force headless (no windows even if window mode chosen)" << std::endl;
    std::cout << "  --list-json          List detected sensors in JSON and exit" << std::endl;
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
    ViewOutputMode output_mode = ViewOutputMode::TEXT;
    std::string record_file = ""; // Empty = no recording
    std::string playback_file = ""; // Empty = no playback
    bool playback_loop = false;
    bool headless = false;
    bool list_json = false;
    double playback_fps = 30.0;

    // Parse command line arguments
    bool list_only = false;
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
        } else if (arg == "-m" || arg == "--mode" || arg == "-w" || arg == "--window") {
            if (i + 1 < argc) {
                std::string mode_str = argv[++i];
                if (mode_str == "text") output_mode = ViewOutputMode::TEXT;
                else if (mode_str == "ascii" || mode_str == "depth-ascii") output_mode = ViewOutputMode::DEPTH_ASCII;
                else if (mode_str == "depth-stats") output_mode = ViewOutputMode::DEPTH_STATS;
                else if (mode_str == "color-stats") output_mode = ViewOutputMode::COLOR_STATS;
                else if (mode_str == "depth-window" || mode_str == "depth" || mode_str == "window") output_mode = ViewOutputMode::DEPTH_WINDOW;
                else if (mode_str == "color-window" || mode_str == "color") output_mode = ViewOutputMode::COLOR_WINDOW;
                else {
                    std::cerr << "Error: Invalid mode: " << mode_str << std::endl;
                    std::cerr << "Valid: text, ascii, depth-stats, color-stats, depth-window, color-window" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --mode requires a value" << std::endl;
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
        } else if (arg == "-l" || arg == "--list") {
            list_only = true;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--list-json") {
            list_json = true;
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

    // Enumerate sensors if requested or if auto mode with potential selection
    auto sensors = caldera::backend::tools::enumerateSensors();
    if (list_only || list_json) {
        if (sensors.empty()) {
            if (list_json) std::cout << "[]" << std::endl; else std::cout << "No sensors detected." << std::endl;
        } else {
            if (list_json) {
                std::cout << "[";
                for (size_t i=0;i<sensors.size();++i) {
                    if (i) std::cout << ",";
                    std::cout << "{\"index\":"<<i<<",\"type\":\"" << (sensors[i].type==caldera::backend::tools::SensorType::KINECT_V2?"KINECT_V2":"KINECT_V1") << "\",\"id\":\""<<sensors[i].id<<"\"}";
                }
                std::cout << "]" << std::endl;
            } else {
                std::cout << "Detected sensors:" << std::endl;
                for (size_t i=0;i<sensors.size();++i) {
                    std::cout << "  ["<<i<<"] " << (sensors[i].type==caldera::backend::tools::SensorType::KINECT_V2?"KinectV2":"KinectV1") << " id=" << sensors[i].id << std::endl;
                }
            }
        }
        return 0;
    }

    if (sensor_type == caldera::backend::tools::SensorType::AUTO_DETECT && sensors.size()>1) {
        // Interactive selection only if TTY and no explicit sensor requested
        if (isatty(0)) {
            std::cout << "Multiple sensors detected:" << std::endl;
            for (size_t i=0;i<sensors.size();++i) {
                std::cout << "  ("<<i<<") " << (sensors[i].type==caldera::backend::tools::SensorType::KINECT_V2?"KinectV2":"KinectV1") << " id=" << sensors[i].id << std::endl;
            }
            std::cout << "Select sensor index: " << std::flush;
            std::string line; std::getline(std::cin, line);
            try {
                size_t idx = std::stoul(line);
                if (idx < sensors.size()) {
                    sensor_type = sensors[idx].type;
                }
            } catch (...) {
                std::cerr << "Invalid selection, continuing with default preference." << std::endl;
            }
        } else {
            // Non-interactive: prefer V2
            for (auto &s : sensors) if (s.type == caldera::backend::tools::SensorType::KINECT_V2) { sensor_type = s.type; break; }
            if (sensor_type == caldera::backend::tools::SensorType::AUTO_DETECT) sensor_type = sensors.front().type;
        }
    }

    // Create viewers based on window mode
    std::unique_ptr<caldera::backend::tools::SimpleViewer> simple_viewer;
    std::unique_ptr<caldera::backend::tools::VideoWindow> video_window;
    
    if (output_mode == ViewOutputMode::DEPTH_ASCII || output_mode == ViewOutputMode::DEPTH_STATS || output_mode == ViewOutputMode::COLOR_STATS) {
        simple_viewer = std::make_unique<caldera::backend::tools::SimpleViewer>("Kinect Data Viewer");
    } else if (output_mode == ViewOutputMode::DEPTH_WINDOW) {
        video_window = std::make_unique<caldera::backend::tools::VideoWindow>("Kinect Depth Stream", 640, 480);
    } else if (output_mode == ViewOutputMode::COLOR_WINDOW) {
        video_window = std::make_unique<caldera::backend::tools::VideoWindow>("Kinect Color Stream", 960, 540);
    }

    // Headless / DISPLAY check
    if (!headless) {
        const char* disp = std::getenv("DISPLAY");
        if (!disp && (output_mode == ViewOutputMode::DEPTH_WINDOW || output_mode == ViewOutputMode::COLOR_WINDOW)) {
            std::cerr << "No DISPLAY detected; forcing headless stats mode." << std::endl;
            headless = true;
        }
    }
    if (headless && (output_mode == ViewOutputMode::DEPTH_WINDOW || output_mode == ViewOutputMode::COLOR_WINDOW)) {
        // Fallback to stats
        output_mode = (output_mode == ViewOutputMode::DEPTH_WINDOW) ? ViewOutputMode::DEPTH_STATS : ViewOutputMode::COLOR_STATS;
        video_window.reset();
        if (output_mode == ViewOutputMode::DEPTH_STATS || output_mode == ViewOutputMode::COLOR_STATS) {
            if (!simple_viewer) simple_viewer = std::make_unique<caldera::backend::tools::SimpleViewer>("Headless Viewer");
        }
    }

    // Create appropriate viewer
    std::unique_ptr<SensorViewerCore> viewer;
    if (!playback_file.empty()) {
        viewer = std::make_unique<SensorViewerCore>(playback_file, caldera::backend::tools::ViewMode::TEXT_ONLY);
        viewer->setPlaybackOptions(playback_loop, playback_fps);
    } else {
        viewer = std::make_unique<SensorViewerCore>(sensor_type, caldera::backend::tools::ViewMode::TEXT_ONLY);
    }

    // Set up callbacks for visual display
    if (simple_viewer) {
        if (output_mode == ViewOutputMode::DEPTH_ASCII) {
            viewer->setDepthFrameCallback([&simple_viewer](const caldera::backend::common::RawDepthFrame& frame) {
                simple_viewer->showDepthASCII(frame);
            });
        } else if (output_mode == ViewOutputMode::DEPTH_STATS) {
            viewer->setDepthFrameCallback([&simple_viewer](const caldera::backend::common::RawDepthFrame& frame) {
                simple_viewer->showDepthFrame(frame);
            });
        } else if (output_mode == ViewOutputMode::COLOR_STATS) {
            viewer->setColorFrameCallback([&simple_viewer](const caldera::backend::common::RawColorFrame& frame) {
                simple_viewer->showColorFrame(frame);
            });
        }
    }
    
    // For windowed modes we buffer frames and render from main thread to avoid OpenGL usage from capture thread
    std::mutex depth_frame_mutex;
    std::mutex color_frame_mutex;
    std::shared_ptr<caldera::backend::common::RawDepthFrame> pending_depth_frame;
    std::shared_ptr<caldera::backend::common::RawColorFrame> pending_color_frame;

    if (video_window) {
        if (output_mode == ViewOutputMode::DEPTH_WINDOW) {
            viewer->setDepthFrameCallback([&depth_frame_mutex,&pending_depth_frame](const caldera::backend::common::RawDepthFrame& frame){
                auto copy = std::make_shared<caldera::backend::common::RawDepthFrame>(frame);
                std::lock_guard<std::mutex> lk(depth_frame_mutex);
                pending_depth_frame = std::move(copy);
            });
        } else if (output_mode == ViewOutputMode::COLOR_WINDOW) {
            viewer->setColorFrameCallback([&color_frame_mutex,&pending_color_frame](const caldera::backend::common::RawColorFrame& frame){
                auto copy = std::make_shared<caldera::backend::common::RawColorFrame>(frame);
                std::lock_guard<std::mutex> lk(color_frame_mutex);
                pending_color_frame = std::move(copy);
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
            // Timed mode. If windowed, we need an explicit render loop; otherwise delegate to viewer->runFor.
            if (video_window) {
                bool window_shown = false;
                auto start = std::chrono::steady_clock::now();
                auto deadline = start + std::chrono::seconds(run_time);
                while (viewer->isRunning() && !should_exit.load() && std::chrono::steady_clock::now() < deadline) {
                    if (!window_shown) { video_window->show(); window_shown = true; }
                    video_window->pollEvents();
                    if (video_window->shouldClose()) break;

                    if (output_mode == ViewOutputMode::DEPTH_WINDOW) {
                        std::shared_ptr<caldera::backend::common::RawDepthFrame> frame_to_draw;
                        { std::lock_guard<std::mutex> lk(depth_frame_mutex); frame_to_draw = pending_depth_frame; }
                        if (frame_to_draw) video_window->showDepthFrame(*frame_to_draw);
                    } else if (output_mode == ViewOutputMode::COLOR_WINDOW) {
                        std::shared_ptr<caldera::backend::common::RawColorFrame> frame_to_draw;
                        { std::lock_guard<std::mutex> lk(color_frame_mutex); frame_to_draw = pending_color_frame; }
                        if (frame_to_draw) video_window->showColorFrame(*frame_to_draw);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            } else {
                viewer->runFor(run_time);
            }
        } else {
            bool window_shown = false;
            while (viewer->isRunning() && !should_exit.load()) {
                // Handle video window events
                if (video_window) {
                    if (!window_shown) { video_window->show(); window_shown = true; }
                    video_window->pollEvents();
                    if (video_window->shouldClose()) {
                        break;
                    }
                    // Render pending frame (depth or color)
                    if (output_mode == ViewOutputMode::DEPTH_WINDOW) {
                        std::shared_ptr<caldera::backend::common::RawDepthFrame> frame_to_draw;
                        {
                            std::lock_guard<std::mutex> lk(depth_frame_mutex);
                            frame_to_draw = pending_depth_frame;
                        }
                        if (frame_to_draw) {
                            video_window->showDepthFrame(*frame_to_draw);
                        }
                    } else if (output_mode == ViewOutputMode::COLOR_WINDOW) {
                        std::shared_ptr<caldera::backend::common::RawColorFrame> frame_to_draw;
                        {
                            std::lock_guard<std::mutex> lk(color_frame_mutex);
                            frame_to_draw = pending_color_frame;
                        }
                        if (frame_to_draw) {
                            video_window->showColorFrame(*frame_to_draw);
                        }
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

        // IMPORTANT: Destroy the viewer (and thus sensor device & libfreenect2 resources)
        // BEFORE we tear down any GLFW / OpenGL state. Some packet pipelines (e.g. VAAPI)
        // may still reference display/DRM handles during their destruction. Previously we
        // destroyed the window (calling glfwTerminate when last window) first, which could
        // lead to a segmentation fault during libfreenect2's VAAPI cleanup.
        viewer.reset();

        // Now it is safe to destroy the video window / GLFW
        if (video_window) {
            video_window.reset();
        }

        // Cleanup loggers (optional skip for debugging shutdown crashes)
        const char* skip_logger = std::getenv("CALDERA_SKIP_LOGGER_SHUTDOWN");
        if (!(skip_logger && (std::string(skip_logger)=="1" || std::string(skip_logger)=="true"))) {
            spdlog::shutdown();
        } else {
            std::cerr << "Skipping spdlog::shutdown due to CALDERA_SKIP_LOGGER_SHUTDOWN" << std::endl;
        }
        
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