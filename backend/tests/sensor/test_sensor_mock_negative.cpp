#include <gtest/gtest.h>
#include "hal/MockSensorDevice.h"
#include "common/Logger.h"
#include <string>

using caldera::backend::hal::MockSensorDevice;

// Separate negative-path suite so log noise is clearly attributable.
class MockSensorDeviceNegative : public ::testing::Test {
protected:
    void SetUp() override {
        if (!caldera::backend::common::Logger::instance().isInitialized()) {
            caldera::backend::common::Logger::instance().initialize("logs/test/mock_sensor_negative.log");
        }
    }
};

TEST_F(MockSensorDeviceNegative, MissingFileOpenFails_ExpectedErrors) {
    // Intentionally use a file that (very likely) does not exist.
    const std::string missing = "nonexistent_mock_file_12345abcdef.dat";
    MockSensorDevice mock(missing);
    // Expect open to fail without throwing. We intentionally DO NOT capture async spdlog output
    // (would require synchronization); visible error lines are expected and documented below.
    EXPECT_FALSE(mock.open());
    EXPECT_FALSE(mock.isDataLoaded());
    // Expected logged lines (example):
    //   [HAL.MockSensorDevice] [error] Data file does not exist: <file>
    //   [HAL.MockSensorDevice] [error] Failed to load data file: <file>
}
