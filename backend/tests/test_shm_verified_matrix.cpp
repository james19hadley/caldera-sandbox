#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>

#include "common/Logger.h"
#include "common/DataTypes.h"
#include "common/Checksum.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"
#include "common/StatsUtil.h"
#include "TestFramePattern.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::common::WorldFrame;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;
using caldera::backend::common::crc32;
using caldera::backend::common::prettyFps;


struct VMScenario { int w; int h; double fps; int seconds; double min_ratio; };

TEST(SharedMemoryVerifiedMatrix, MultiSizeFpsCoverage) { // Combines size + rate + verification
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_verified_matrix.log");
    }
    Logger::instance().setGlobalLevel(spdlog::level::info);
    // Capacity large enough for biggest scenario
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_verified_matrix"; cfg.max_width=1024; cfg.max_height=1024; cfg.checksum_interval_ms=0;
    SharedMemoryTransportServer server(Logger::instance().get("Test.SHM.VerifiedMatrix.Server"), cfg); server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.VerifiedMatrix.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    std::vector<VMScenario> scenarios = {
        {128, 128,  60.0, 1, 0.90},
        {256, 256, 120.0, 1, 0.70},
        {512, 256,  90.0, 1, 0.80},
        {640, 480,  30.0, 1, 0.95},
    };

    uint64_t global_frame_id = 0;
    uint64_t prev_published_cumulative = 0;
    uint64_t prev_attempted_cumulative = 0;
    uint64_t prev_dropped_cumulative   = 0;
    for (auto sc : scenarios) {
        WorldFrame wf; wf.heightMap.width=sc.w; wf.heightMap.height=sc.h; wf.heightMap.data.resize(sc.w * sc.h);
        const int total_frames = static_cast<int>(sc.fps * sc.seconds);
        std::atomic<uint64_t> verified{0};
        std::atomic<uint64_t> last_seen{UINT64_MAX};
        std::atomic<bool> stop{false};
        std::atomic<uint64_t> gaps{0};

        // Record start frame id for this scenario so we ignore any residual frame from previous scenario.
        uint64_t start_frame_id = global_frame_id;

        std::thread consumer([&]{
            while(!stop.load()) {
                auto opt = reader.latest();
                if (opt && opt->frame_id != last_seen.load(std::memory_order_relaxed)) {
                    // Ignore stale frame from previous scenario
                    if (opt->frame_id < start_frame_id) {
                        last_seen.store(opt->frame_id, std::memory_order_relaxed);
                        continue;
                    }
                    uint64_t prev = last_seen.load(std::memory_order_relaxed);
                    if (prev != UINT64_MAX && opt->frame_id > prev + 1) {
                        gaps.fetch_add(opt->frame_id - prev - 1, std::memory_order_relaxed);
                    }
                    last_seen.store(opt->frame_id, std::memory_order_relaxed);
                    if (opt->checksum) {
                        auto c = crc32(opt->data, opt->float_count);
                        if (c == opt->checksum) verified.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                std::this_thread::sleep_for(1ms); // moderate polling
            }
        });

        auto frame_period = std::chrono::duration<double>(1.0 / sc.fps);
        auto start = std::chrono::steady_clock::now();
        for (int i=0;i<total_frames;++i) {
            wf.frame_id = global_frame_id++;
            wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            caldera::backend::tests::fillDeterministicPattern(wf, wf.frame_id);
            wf.checksum = crc32(wf.heightMap.data); // explicit checksum every frame
            server.sendWorldFrame(wf);
            std::this_thread::sleep_until(start + std::chrono::duration_cast<std::chrono::nanoseconds>(frame_period * (i+1)));
        }
        std::this_thread::sleep_for(150ms); // tail time
        stop.store(true); consumer.join();
    int got = static_cast<int>(verified.load());
    if (got > total_frames) got = total_frames; // safety clamp (should not trigger after fix)
    double ratio = (double)got / (double)total_frames;
        auto stats = server.snapshotStats();
        // Derive per-scenario deltas instead of cumulative values
        uint64_t scen_published = stats.frames_published - prev_published_cumulative;
        uint64_t scen_attempted = stats.frames_attempted - prev_attempted_cumulative;
        uint64_t scen_dropped   = stats.frames_dropped_capacity - prev_dropped_cumulative;
        prev_published_cumulative = stats.frames_published;
        prev_attempted_cumulative = stats.frames_attempted;
        prev_dropped_cumulative   = stats.frames_dropped_capacity;
        Logger::instance().get("Test.SHM.VerifiedMatrix.Report")->info(
            "size={}x{} fps={} attempted={} published={} dropped={} verified={}/{} ratio={:.2f} gaps={} writer_fps_est={}",
            sc.w, sc.h, sc.fps, scen_attempted, scen_published, scen_dropped, got, total_frames, ratio, gaps.load(), prettyFps(stats.last_publish_fps));
        EXPECT_GE(ratio, sc.min_ratio) << "Scenario failed size=" << sc.w << "x" << sc.h << " fps=" << sc.fps;
    }

    server.stop(); shm_unlink(cfg.shm_name.c_str());
}
