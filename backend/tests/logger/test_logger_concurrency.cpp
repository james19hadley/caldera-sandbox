#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "common/Logger.h"

using caldera::backend::common::Logger;

TEST(LoggerConcurrency, ParallelGetAndOverride) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/logger_concurrency.log");
        Logger::instance().setGlobalLevel(spdlog::level::info);
    }
    // Silence noisy debug output for this stress scenario
    auto ctlLogger = Logger::instance().get("Conc.Log.ctl");
    auto previousLevel = ctlLogger->level();
    // Raise global verbosity threshold to off to suppress debug completely in this stress test
    Logger::instance().setGlobalLevel(spdlog::level::off);
    constexpr int Threads = 8;
    constexpr int Iter = 2000;
    std::atomic<int> overrides{0};

    auto worker = [&]() {
        for (int i=0;i<Iter;++i) {
            auto lg = Logger::instance().get("Conc.Log." + std::to_string(i % 5));
            if (i % 400 == 0) {
                // Still exercise override path but keep level off for silence
                Logger::instance().setLoggerLevel(lg->name(), spdlog::level::off);
                overrides++;
            }
            // Intentionally suppressed; level is off
            lg->debug("noop {}", i);
        }
    };

    std::vector<std::thread> ts;
    ts.reserve(Threads);
    for (int t=0;t<Threads;++t) ts.emplace_back(worker);
    for (auto &th : ts) th.join();

    EXPECT_GT(overrides.load(), 0);
    Logger::instance().setGlobalLevel(previousLevel);
}
