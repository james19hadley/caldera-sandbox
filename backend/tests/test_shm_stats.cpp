#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include "common/Logger.h"
#include "common/DataTypes.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"
#include "common/StatsUtil.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::common::WorldFrame;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;
using caldera::backend::common::prettyFps;

TEST(SharedMemoryStats, PublishAndCapacityDrops) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_stats.log");
    }
    Logger::instance().setGlobalLevel(spdlog::level::info);
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_stats"; cfg.max_width=128; cfg.max_height=128; // under hard cap
    SharedMemoryTransportServer server(Logger::instance().get("Test.SHM.Stats"), cfg);
    server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Stats.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    WorldFrame wf; wf.heightMap.width=64; wf.heightMap.height=64; wf.heightMap.data.assign(64*64, 0.5f);
    for (int i=0;i<50;++i){ wf.frame_id=i; wf.timestamp_ns= std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); server.sendWorldFrame(wf); }

    // Now send oversized frames to trigger drops
    wf.heightMap.width=256; wf.heightMap.height=256; wf.heightMap.data.assign(256*256, 1.0f);
    for (int i=0;i<10;++i){ wf.frame_id=100+i; server.sendWorldFrame(wf); }

    auto st = server.snapshotStats();
    Logger::instance().get("Test.SHM.Stats.Report")->info(
        "attempted={} published={} dropped={} bytes={} writer_fps_est={}",
        st.frames_attempted, st.frames_published, st.frames_dropped_capacity,
        st.bytes_written, prettyFps(st.last_publish_fps));
    server.stop(); shm_unlink(cfg.shm_name.c_str());
    EXPECT_EQ(st.frames_published, 50u);
    EXPECT_EQ(st.frames_dropped_capacity, 10u);
    EXPECT_GT(st.bytes_written, 0u);
    EXPECT_GT(st.last_publish_fps, 0.0);
}

// Multi-size FPS scenario collecting stats
TEST(SharedMemoryStats, MultiSizeFpsScenario) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_stats.log");
    }
    Logger::instance().setGlobalLevel(spdlog::level::info);
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_stats_fps"; cfg.max_width=512; cfg.max_height=512; // under hard cap
    SharedMemoryTransportServer server(Logger::instance().get("Test.SHM.StatsFps"), cfg); server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.StatsFps.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    struct Mode { int w; int h; double fps; int seconds; } modes[] = {
        {128,128, 60.0, 1},
        {256,256, 30.0, 1},
        {512,128,120.0, 1}
    };

    WorldFrame wf; wf.heightMap.data.reserve(512*512);
    uint64_t fid=0;
    for (auto m : modes) {
        wf.heightMap.width=m.w; wf.heightMap.height=m.h; wf.heightMap.data.assign(m.w*m.h, 0.25f);
        int total = (int)(m.fps * m.seconds);
        auto frame_period = std::chrono::duration<double>(1.0/m.fps);
        auto start = std::chrono::steady_clock::now();
        for(int i=0;i<total;++i){
            wf.frame_id = fid++; wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            server.sendWorldFrame(wf);
            std::this_thread::sleep_until(start + std::chrono::duration_cast<std::chrono::nanoseconds>(frame_period * (i+1)));
        }
    }
    auto st = server.snapshotStats();
    Logger::instance().get("Test.SHM.Stats.Report")->info(
        "attempted={} published={} dropped={} bytes={} writer_fps_est={}",
        st.frames_attempted, st.frames_published, st.frames_dropped_capacity,
        st.bytes_written, prettyFps(st.last_publish_fps));
    server.stop(); shm_unlink(cfg.shm_name.c_str());
    EXPECT_EQ(st.frames_dropped_capacity, 0u);
    EXPECT_GT(st.frames_published, 0u);
    EXPECT_GT(st.bytes_written, 0u);
    EXPECT_GT(st.last_publish_fps, 0.0);
}
