#include "hal/MockSensorDevice.h"
#include "common/Logger.h"
#include <filesystem>
#include <chrono>
#include <thread>

namespace caldera::backend::hal {

MockSensorDevice::MockSensorDevice(const std::string& dataFile) 
    : data_file_(dataFile) {
    logger_ = common::Logger::instance().get("HAL.MockSensorDevice");
}

MockSensorDevice::~MockSensorDevice() noexcept {
    try {
        is_running_.store(false);
        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }
    } catch (...) {
        // Swallow all exceptions in destructor
    }
}

bool MockSensorDevice::open() {
    if (is_running_.load()) {
        logger_->warn("Mock sensor already running");
        return true;
    }

    if (!loadDataFile()) {
        logger_->error("Failed to load data file: " + data_file_);
        return false;
    }

    logger_->info("Mock sensor opened with " + std::to_string(frame_count_) + " frames");
    return true;
}

void MockSensorDevice::close() {
    try {
        if (is_running_.load()) {
            is_running_.store(false);
            if (playback_thread_.joinable()) {
                playback_thread_.join();
            }
        }
    } catch (const std::exception& e) {
        // Log error but don't throw from close()
        if (logger_) {
            logger_->error("Error in MockSensorDevice::close(): " + std::string(e.what()));
        }
    } catch (...) {
        // Ignore other exceptions in close()
    }
}

void MockSensorDevice::setFrameCallback(RawFrameCallback callback) {
    frame_callback_ = callback;
    
    if (callback && data_loaded_ && !is_running_.load()) {
        is_running_.store(true);
        playback_thread_ = std::thread(&MockSensorDevice::playbackLoop, this);
    }
}

bool MockSensorDevice::loadDataFile() {
    if (!std::filesystem::exists(data_file_)) {
        logger_->error("Data file does not exist: " + data_file_);
        return false;
    }

    std::ifstream file(data_file_, std::ios::binary);
    if (!file.is_open()) {
        logger_->error("Cannot open data file: " + data_file_);
        return false;
    }

    try {
        // Read header
        uint32_t magic, version, frame_count;
        file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&frame_count), sizeof(uint32_t));

        if (magic != MAGIC_NUMBER) {
            logger_->error("Invalid file format (bad magic number)");
            return false;
        }

        if (version != FILE_VERSION) {
            logger_->error("Unsupported file version: " + std::to_string(version));
            return false;
        }

        // Skip reserved space
        file.seekg(sizeof(uint32_t) * 5, std::ios::cur);

        frame_count_ = frame_count;
        frames_.resize(frame_count_);

        logger_->debug("Loading " + std::to_string(frame_count_) + " frames from file");

        // Read all frames
        for (size_t i = 0; i < frame_count_; ++i) {
            auto& frame_data = frames_[i];
            
            // Read timestamp
            file.read(reinterpret_cast<char*>(&frame_data.timestamp_ns), sizeof(uint64_t));
            
            // Read depth frame
            uint32_t depth_width, depth_height, depth_size;
            file.read(reinterpret_cast<char*>(&depth_width), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&depth_height), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&depth_size), sizeof(uint32_t));
            
            frame_data.depth.sensorId = getDeviceID();
            frame_data.depth.timestamp_ns = frame_data.timestamp_ns;
            frame_data.depth.width = depth_width;
            frame_data.depth.height = depth_height;
            frame_data.depth.data.resize(depth_size);
            
            file.read(reinterpret_cast<char*>(frame_data.depth.data.data()), 
                     depth_size * sizeof(uint16_t));
            
            // Read color frame
            uint32_t color_width, color_height, color_size;
            file.read(reinterpret_cast<char*>(&color_width), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&color_height), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&color_size), sizeof(uint32_t));
            
            frame_data.color.sensorId = getDeviceID();
            frame_data.color.timestamp_ns = frame_data.timestamp_ns;
            frame_data.color.width = color_width;
            frame_data.color.height = color_height;
            frame_data.color.data.resize(color_size);
            
            file.read(reinterpret_cast<char*>(frame_data.color.data.data()), color_size);
        }

        data_loaded_ = true;
        logger_->info("Successfully loaded " + std::to_string(frame_count_) + " frames");
        return true;

    } catch (const std::exception& e) {
        logger_->error("Error reading data file: " + std::string(e.what()));
        return false;
    }
}

void MockSensorDevice::playbackLoop() {
    if (!frame_callback_ || frames_.empty()) {
        is_running_.store(false);
        return;
    }

    auto frame_duration = std::chrono::duration<double>(1.0 / playback_fps_);
    size_t current_frame = 0;
    int loops_played = 0;

    logger_->debug("Starting playback at " + std::to_string(playback_fps_) + " FPS");

    while (is_running_.load()) {
        auto start_time = std::chrono::steady_clock::now();

        // Send current frame
        const auto& frame_data = frames_[current_frame];
        if (frame_callback_) {
            frame_callback_(frame_data.depth, frame_data.color);
        }

        // Advance to next frame
        current_frame++;
        
        // Handle different playback modes
        if (current_frame >= frames_.size()) {
            current_frame = 0;
            loops_played++;

            if (playback_mode_ == PlaybackMode::SINGLE_FRAME) {
                // Stay on first frame
                current_frame = 0;
            } else if (playback_mode_ == PlaybackMode::ONCE) {
                // Stop after one pass
                break;
            } else if (playback_mode_ == PlaybackMode::LOOP) {
                // Check loop limit
                if (loop_count_ > 0 && loops_played >= loop_count_) {
                    break;
                }
            }
        }

        // Maintain target FPS
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto sleep_time = frame_duration - elapsed;
        if (sleep_time > std::chrono::microseconds(0)) {
            std::this_thread::sleep_for(sleep_time);
        }
    }

    is_running_.store(false);
    logger_->debug("Playback finished. Loops: " + std::to_string(loops_played));
}

} // namespace caldera::backend::hal