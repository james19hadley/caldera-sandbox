#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

#include "common/Logger.h"
#include "transport/LocalTransportServer.h"
#include "transport/SharedMemoryTransportServer.h"
#include "TestLocalTransportClient.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::transport::LocalTransportServer;
using caldera::backend::transport::SharedMemoryTransportServer;

// Integration: handshake + periodic server stats emission over s2c FIFO
TEST(HandshakeStats, PeriodicServerStatsJson) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/handshake_stats.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto srvLog = Logger::instance().get("Test.HandshakeStats.Server");
    auto hsLog  = Logger::instance().get("Test.HandshakeStats.Trace");

    // Underlying SHM transport to supply stats
    SharedMemoryTransportServer::Config shmCfg; shmCfg.shm_name="/caldera_hs_stats"; shmCfg.max_width=64; shmCfg.max_height=64;
    auto shmServer = std::make_shared<SharedMemoryTransportServer>(Logger::instance().get("Test.HandshakeStats.SHM"), shmCfg);
    shmServer->start();

    LocalTransportServer::Config cfg; cfg.pipe_s2c="/tmp/caldera_s2c_hs_stats"; cfg.pipe_c2s="/tmp/caldera_c2s_hs_stats"; cfg.server_stats_interval_ms=250; // 4 per second
    auto server = std::make_shared<LocalTransportServer>(srvLog, hsLog, cfg);
    server->setStatsJsonProvider([shmServer]{
        auto s = shmServer->snapshotStats();
        char buf[256];
        // Simple compact JSON
        snprintf(buf, sizeof(buf), "{\"type\":\"server_stats\",\"frames_published\":%llu,\"attempted\":%llu,\"dropped\":%llu,\"fps\":%.2f}",
                 (unsigned long long)s.frames_published,
                 (unsigned long long)s.frames_attempted,
                 (unsigned long long)s.frames_dropped_capacity,
                 s.last_publish_fps);
        return std::string(buf);
    });
    server->start();
    std::this_thread::sleep_for(50ms);

    TestLocalTransportClient client(Logger::instance().get("Test.HandshakeStats.Client"));
    ASSERT_TRUE(client.handshake({cfg.pipe_s2c, cfg.pipe_c2s})) << "Handshake failed";

    // Publish some frames to change stats
    caldera::backend::common::WorldFrame wf; wf.heightMap.width=32; wf.heightMap.height=32; wf.heightMap.data.assign(32*32, 0.1f);
    for (int i=0;i<20;++i) {
        wf.frame_id = i; wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        shmServer->sendWorldFrame(wf);
        std::this_thread::sleep_for(10ms);
    }

    // Collect lines for up to 2s (non-blocking loop)
    // Collect server stats via helper (needs at least 3 lines)
    auto lines = client.collectServerStats(3, 3000);
    server->stop(); shmServer->stop(); client.closeAll();
    std::string all; for (auto& l : lines) { all += l + "\n"; }
    ASSERT_GE((int)lines.size(), 3) << "Too few stats lines captured. Raw: " << all;
    ASSERT_NE(all.find("\"frames_published\""), std::string::npos);
    ASSERT_NE(all.find("\"fps\""), std::string::npos);
}
