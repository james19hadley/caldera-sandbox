#include <gtest/gtest.h>
#include "common/Logger.h"

using caldera::backend::common::Logger;

TEST(LoggerBasic, InitializeAndGet) {
    // Ensure clean state (in case tests run multiple times)
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/logger_basic.log");
    }
    auto log = Logger::instance().get("TEST_MODULE");
    ASSERT_NE(log, nullptr);
    log->info("Test info message");
}

TEST(LoggerBasic, PerLoggerOverride) {
    Logger::instance().setLoggerLevel("TEST_MODULE_OVERRIDE", spdlog::level::trace);
    auto log = Logger::instance().get("TEST_MODULE_OVERRIDE");
    EXPECT_TRUE(log->should_log(spdlog::level::trace));
}
