#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>

#include "common/Logger.h"
#include "common/DataTypes.h"
#include "common/Checksum.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"
#include "common/StatsUtil.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::common::WorldFrame;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;
using caldera::backend::common::crc32;
using caldera::backend::common::prettyFps;

// Helper to publish frames at target FPS and measure observed frames by reader.
static void fpsScenario(double target_fps, int seconds, double min_coverage_ratio) {
    auto logT = Logger::instance().get("Test.SHM.Real.Transport" + std::to_string((int)target_fps));
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_fps_" + std::to_string((int)target_fps); cfg.max_width=64; cfg.max_height=64; cfg.checksum_interval_ms=0;
    SharedMemoryTransportServer server(logT, cfg); server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Real.Reader" + std::to_string((int)target_fps)));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    WorldFrame wf; wf.heightMap.width=32; wf.heightMap.height=32; wf.heightMap.data.assign(32*32, 1.f);
    const int total_frames = static_cast<int>(target_fps * seconds);
    std::atomic<int> verified{0};
    std::atomic<uint64_t> last_id{UINT64_MAX};
    std::atomic<bool> stop{false};

    std::thread consumer([&]{
        while(!stop.load()) {
            auto opt = reader.latest();
            if (opt && opt->frame_id != last_id.load()) {
                last_id.store(opt->frame_id);
                if (opt->checksum) {
                    auto c = crc32(opt->data, opt->float_count);
                    if (c == opt->checksum) verified.fetch_add(1);
                }
            }
            std::this_thread::sleep_for(2ms); // realistic polling cadence
        }
    });

    auto frame_period = std::chrono::duration<double>(1.0/target_fps);
    auto start = std::chrono::steady_clock::now();
    for (int i=0;i<total_frames; ++i) {
        auto next_time = start + std::chrono::duration_cast<std::chrono::nanoseconds>(frame_period * (i+1));
        wf.frame_id = i; wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        wf.checksum = crc32(wf.heightMap.data);
        server.sendWorldFrame(wf);
        std::this_thread::sleep_until(next_time);
    }
    // Give reader tail time
    std::this_thread::sleep_for(200ms);
    stop.store(true); consumer.join();
    int got = verified.load();
    double ratio = (double)got / (double)total_frames;
    auto stats = server.snapshotStats();
    Logger::instance().get("Test.SHM.Realistic.Report")->info(
        "target={} total_pub={} dropped={} bytes={} writer_fps_est={} verified={}/{} ratio={:.2f}",
        target_fps, stats.frames_published, stats.frames_dropped_capacity, stats.bytes_written,
        prettyFps(stats.last_publish_fps), got, total_frames, ratio);
    server.stop(); shm_unlink(cfg.shm_name.c_str());
    // We expect at least min_coverage_ratio of frames to be observed+verified at this gentle pace.
    EXPECT_GE(ratio, min_coverage_ratio) << "Coverage too low: got=" << got << " total=" << total_frames << " ratio=" << ratio;
    // Integrity check: monotonic last id
    EXPECT_GE(last_id.load(), static_cast<uint64_t>(total_frames-1));
}

TEST(SharedMemoryRealisticFPS, Fps30Scenario) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_realistic_fps.log");
    }
    Logger::instance().setGlobalLevel(spdlog::level::info);
    fpsScenario(30.0, 2, 0.9); // 2s at 30 FPS -> expect >=90% observed
}

TEST(SharedMemoryRealisticFPS, Fps120Scenario) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_realistic_fps.log");
    }
    Logger::instance().setGlobalLevel(spdlog::level::info);
    fpsScenario(120.0, 2, 0.7); // 2s at 120 FPS -> expect >=70% observed
}
