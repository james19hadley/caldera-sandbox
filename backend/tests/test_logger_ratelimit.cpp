#include <gtest/gtest.h>
#include "common/Logger.h"
#include <chrono>
#include <thread>

using caldera::backend::common::Logger;

TEST(LoggerRateLimit, EmitsOnceWithinPeriod) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/ratelimit.log", spdlog::level::info);
        Logger::instance().setGlobalLevel(spdlog::level::info);
    }
    auto name = "Test.RateLimit";
    Logger::instance().warnRateLimited(name, "key1", std::chrono::milliseconds(200), "First warning");
    Logger::instance().warnRateLimited(name, "key1", std::chrono::milliseconds(200), "Second warning (should be suppressed)");
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    Logger::instance().warnRateLimited(name, "key1", std::chrono::milliseconds(200), "Third warning (after period)");
    SUCCEED();
}
