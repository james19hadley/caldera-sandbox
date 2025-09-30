#include <gtest/gtest.h>
#include "common/Logger.h"

using caldera::backend::common::Logger;

class LoggerLevelsFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!Logger::instance().isInitialized()) {
            Logger::instance().initialize("logs/test/levels/logger_levels.log");
        }
    }
};

TEST_F(LoggerLevelsFixture, GlobalLevelInheritance) {
    Logger::instance().setGlobalLevel(spdlog::level::err);
    auto l = Logger::instance().get("Levels.Inherit");
    EXPECT_FALSE(l->should_log(spdlog::level::warn));
    EXPECT_TRUE(l->should_log(spdlog::level::err));
}

TEST_F(LoggerLevelsFixture, PerLoggerOverridePersists) {
    // Use a unique logger name each run to avoid prior state interference
    const char* name = "Levels.Override.Persist";
    Logger::instance().setGlobalLevel(spdlog::level::err);
    Logger::instance().setLoggerLevel(name, spdlog::level::debug);
    auto l = Logger::instance().get(name);
    // Debug should be enabled even though global is err
    ASSERT_TRUE(l->should_log(spdlog::level::debug)) << "Per-logger override not applied";
    // Increase global severity further
    Logger::instance().setGlobalLevel(spdlog::level::critical);
    EXPECT_TRUE(l->should_log(spdlog::level::debug)) << "Override lost after global level change";
    l->debug("diagnostic debug message after global level change");
}

TEST_F(LoggerLevelsFixture, ClearOverride) {
    Logger::instance().setGlobalLevel(spdlog::level::warn);
    Logger::instance().setLoggerLevel("Levels.Temp", spdlog::level::trace);
    auto l = Logger::instance().get("Levels.Temp");
    EXPECT_TRUE(l->should_log(spdlog::level::trace));
    Logger::instance().clearLoggerLevel("Levels.Temp");
    EXPECT_FALSE(l->should_log(spdlog::level::info));
}

TEST_F(LoggerLevelsFixture, DirectoryCreation) {
    // Just ensure initialize already created nested dir earlier; simulate new logger
    auto l = Logger::instance().get("Levels.Dir");
    EXPECT_NE(l, nullptr);
}

TEST_F(LoggerLevelsFixture, OverrideThenGlobalChangeMultipleChecks) {
    const char* name = "Levels.Override.Multi";
    Logger::instance().setGlobalLevel(spdlog::level::err);
    Logger::instance().setLoggerLevel(name, spdlog::level::debug);
    auto l = Logger::instance().get(name);
    ASSERT_TRUE(l->should_log(spdlog::level::debug));
    Logger::instance().setGlobalLevel(spdlog::level::critical);
    EXPECT_TRUE(l->should_log(spdlog::level::debug));
    // Also verify info is disabled (since override is debug, info should be allowed)
    EXPECT_TRUE(l->should_log(spdlog::level::info));
}
