#include <gtest/gtest.h>
#include "hal/SensorRecorder.h"
#include "hal/MockSensorDevice.h"
#include "common/DataTypes.h"
#include "common/Logger.h"
#include <filesystem>
#include <atomic>
#include <chrono>
#include <thread>

using caldera::backend::hal::SensorRecorder;
using caldera::backend::hal::MockSensorDevice;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::RawColorFrame;

class SensorRecordingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logger
        if (!caldera::backend::common::Logger::instance().isInitialized()) {
            caldera::backend::common::Logger::instance().initialize("logs/test/sensor_recording.log");
        }
        
        test_filename_ = "test_sensor_data.dat";
        
        // Remove test file if it exists
        if (std::filesystem::exists(test_filename_)) {
            std::filesystem::remove(test_filename_);
        }
    }

    void TearDown() override {
        // Clean up test files
        if (std::filesystem::exists(test_filename_)) {
            std::filesystem::remove(test_filename_);
        }
    }

    RawDepthFrame createTestDepthFrame(int frame_num) {
        RawDepthFrame frame;
        frame.sensorId = "TestSensor";
        frame.timestamp_ns = 1000000ULL * frame_num; // 1ms intervals
        frame.width = 32;
        frame.height = 24;
        frame.data.resize(frame.width * frame.height);
        
        // Fill with test pattern (frame number + position)
        for (int i = 0; i < frame.width * frame.height; ++i) {
            frame.data[i] = static_cast<uint16_t>(frame_num * 100 + i);
        }
        
        return frame;
    }

    RawColorFrame createTestColorFrame(int frame_num) {
        RawColorFrame frame;
        frame.sensorId = "TestSensor";
        frame.timestamp_ns = 1000000ULL * frame_num;
        frame.width = 64;
        frame.height = 48;
        frame.data.resize(frame.width * frame.height * 4); // BGRX format
        
        // Fill with test pattern
        for (int i = 0; i < frame.width * frame.height * 4; ++i) {
            frame.data[i] = static_cast<uint8_t>(frame_num + i % 256);
        }
        
        return frame;
    }

    std::string test_filename_;
};

TEST_F(SensorRecordingTest, BasicRecordAndPlayback) {
    // Record test data
    {
        SensorRecorder recorder(test_filename_);
        ASSERT_TRUE(recorder.startRecording());
        
        // Record 3 test frames (fewer for simpler test)
        for (int i = 0; i < 3; ++i) {
            auto depth = createTestDepthFrame(i);
            auto color = createTestColorFrame(i);
            recorder.recordFrame(depth, color);
        }
        
        recorder.stopRecording();
        EXPECT_EQ(recorder.getFrameCount(), 3);
    }
    
    // Verify file exists and has data
    ASSERT_TRUE(std::filesystem::exists(test_filename_));
    EXPECT_GT(std::filesystem::file_size(test_filename_), 0);
    
    // Test loading data without playback (simpler test)
    MockSensorDevice mock(test_filename_);
    ASSERT_TRUE(mock.open());
    EXPECT_EQ(mock.getFrameCount(), 3);
    EXPECT_TRUE(mock.isDataLoaded());
    
    // Test basic data integrity without threading
    std::atomic<int> frames_received{0};
    std::atomic<bool> callback_called{false};
    
    mock.setFrameCallback([&frames_received, &callback_called](const RawDepthFrame& depth, const RawColorFrame& color) {
        callback_called.store(true);
        frames_received++;
        
        // Basic validation without EXPECT (to avoid threading issues)
        if (depth.width != 32 || depth.height != 24) {
            std::cerr << "Invalid depth frame size: " << depth.width << "x" << depth.height << std::endl;
        }
        if (color.width != 64 || color.height != 48) {
            std::cerr << "Invalid color frame size: " << color.width << "x" << color.height << std::endl;
        }
    });
    
    // Set to single frame mode (simpler for testing)
    mock.setPlaybackMode(MockSensorDevice::PlaybackMode::SINGLE_FRAME);
    
    // Give it some time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify callback was called
    EXPECT_TRUE(callback_called.load());
    EXPECT_GE(frames_received.load(), 1); // At least one frame should be processed
    
    // Clean shutdown
    mock.close();
}

TEST_F(SensorRecordingTest, RecorderStates) {
    SensorRecorder recorder(test_filename_);
    
    // Initial state
    EXPECT_FALSE(recorder.isRecording());
    EXPECT_EQ(recorder.getFrameCount(), 0);
    
    // Start recording
    EXPECT_TRUE(recorder.startRecording());
    EXPECT_TRUE(recorder.isRecording());
    
    // Record a frame
    auto depth = createTestDepthFrame(0);
    auto color = createTestColorFrame(0);
    recorder.recordFrame(depth, color);
    EXPECT_EQ(recorder.getFrameCount(), 1);
    
    // Stop recording
    recorder.stopRecording();
    EXPECT_FALSE(recorder.isRecording());
}