#include "hal/SensorRecorder.h"
#include "common/Logger.h"
#include <filesystem>
#include <cstring>

namespace caldera::backend::hal {

SensorRecorder::SensorRecorder(const std::string& filename) 
    : filename_(filename) {
    logger_ = common::Logger::instance().get("HAL.SensorRecorder");
}

SensorRecorder::~SensorRecorder() {
    if (is_recording_) {
        stopRecording();
    }
}

bool SensorRecorder::startRecording() {
    if (is_recording_) {
        logger_->warn("Already recording to: " + filename_);
        return false;
    }

    // Create directory if needed
    auto filepath = std::filesystem::path(filename_);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    file_.open(filename_, std::ios::binary);
    if (!file_.is_open()) {
        logger_->error("Failed to open file for recording: " + filename_);
        return false;
    }

    writeHeader();
    is_recording_ = true;
    frame_count_ = 0;

    logger_->info("Started recording to: " + filename_);
    return true;
}

void SensorRecorder::stopRecording() {
    if (!is_recording_) {
        return;
    }

    is_recording_ = false;
    
    // Update frame count in header
    updateFrameCount();
    
    if (file_.is_open()) {
        file_.close();
    }

    logger_->info("Stopped recording. Frames: " + std::to_string(frame_count_) + 
                  ", File size: " + std::to_string(getFileSizeBytes()) + " bytes");
}

void SensorRecorder::recordFrame(const common::RawDepthFrame& depth, const common::RawColorFrame& color) {
    if (!is_recording_ || !file_.is_open()) {
        return;
    }

    try {
        // Write timestamp (use depth frame timestamp as primary)
        file_.write(reinterpret_cast<const char*>(&depth.timestamp_ns), sizeof(uint64_t));
        
        // Write depth frame
        uint32_t depth_width = static_cast<uint32_t>(depth.width);
        uint32_t depth_height = static_cast<uint32_t>(depth.height);
        uint32_t depth_size = static_cast<uint32_t>(depth.data.size());
        
        file_.write(reinterpret_cast<const char*>(&depth_width), sizeof(uint32_t));
        file_.write(reinterpret_cast<const char*>(&depth_height), sizeof(uint32_t));
        file_.write(reinterpret_cast<const char*>(&depth_size), sizeof(uint32_t));
        file_.write(reinterpret_cast<const char*>(depth.data.data()), depth_size * sizeof(uint16_t));
        
        // Write color frame
        uint32_t color_width = static_cast<uint32_t>(color.width);
        uint32_t color_height = static_cast<uint32_t>(color.height);
        uint32_t color_size = static_cast<uint32_t>(color.data.size());
        
        file_.write(reinterpret_cast<const char*>(&color_width), sizeof(uint32_t));
        file_.write(reinterpret_cast<const char*>(&color_height), sizeof(uint32_t));
        file_.write(reinterpret_cast<const char*>(&color_size), sizeof(uint32_t));
        file_.write(reinterpret_cast<const char*>(color.data.data()), color_size);
        
        frame_count_++;
        
        // Flush periodically for safety
        if (frame_count_ % 30 == 0) {
            file_.flush();
            logger_->debug("Recorded " + std::to_string(frame_count_) + " frames");
        }
        
    } catch (const std::exception& e) {
        logger_->error("Failed to record frame: " + std::string(e.what()));
        stopRecording();
    }
}

void SensorRecorder::writeHeader() {
    // Magic number
    file_.write(reinterpret_cast<const char*>(&MAGIC_NUMBER), sizeof(uint32_t));
    
    // Version
    file_.write(reinterpret_cast<const char*>(&FILE_VERSION), sizeof(uint32_t));
    
    // Frame count placeholder (updated when recording stops)
    uint32_t placeholder_count = 0;
    file_.write(reinterpret_cast<const char*>(&placeholder_count), sizeof(uint32_t));
    
    // Reserved space for future metadata
    uint32_t reserved[5] = {0};
    file_.write(reinterpret_cast<const char*>(reserved), sizeof(reserved));
}

void SensorRecorder::updateFrameCount() {
    if (!file_.is_open()) {
        return;
    }

    // Seek to frame count position (after magic + version)
    file_.seekp(sizeof(uint32_t) * 2);
    
    uint32_t count = static_cast<uint32_t>(frame_count_);
    file_.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
    
    // Seek back to end
    file_.seekp(0, std::ios::end);
}

size_t SensorRecorder::getFileSizeBytes() const {
    if (!std::filesystem::exists(filename_)) {
        return 0;
    }
    
    try {
        return std::filesystem::file_size(filename_);
    } catch (...) {
        return 0;
    }
}

} // namespace caldera::backend::hal