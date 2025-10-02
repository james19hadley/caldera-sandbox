#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>

#include "common/Logger.h"
#include "common/DataTypes.h"
#include "common/Checksum.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::common::WorldFrame;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;

// Measures end-to-end latency between writer timestamp_ns (assigned at send) and
// reader retrieval time. This uses steady_clock to approximate real latency.
TEST(SharedMemoryLatency, BasicMeasurement) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_latency.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Lat.Transport");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_latency"; cfg.max_width=64; cfg.max_height=64;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Lat.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    std::atomic<bool> stop{false};
    std::vector<double> samples_us; samples_us.reserve(200);

    std::thread consumer([&]{
        uint64_t last_id = 0;
        while(!stop.load()) {
            auto opt = reader.latest();
            if (opt && opt->frame_id != last_id) {
                last_id = opt->frame_id;
                auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (opt->timestamp_ns <= now_ns) {
                    double diff_us = (now_ns - opt->timestamp_ns) / 1000.0;
                    samples_us.push_back(diff_us);
                }
            }
            std::this_thread::sleep_for(1ms);
        }
    });

    WorldFrame wf; wf.heightMap.width=32; wf.heightMap.height=32; wf.heightMap.data.resize(32*32, 1.f);
    for (uint64_t i=1; i<=200; ++i) {
        wf.frame_id = i;
        wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        server.sendWorldFrame(wf);
        std::this_thread::sleep_for(2ms); // ~500 FPS pseudo workload
    }
    stop.store(true);
    consumer.join();

    server.stop();
    shm_unlink(cfg.shm_name.c_str());

    ASSERT_GE(samples_us.size(), 50u); // ensure we captured enough
    // Compute simple stats
    double sum=0, maxv=0; for(double v: samples_us){ sum+=v; if(v>maxv) maxv=v; }
    double avg = sum / samples_us.size();
    // Expect sub-millisecond average
    EXPECT_LT(avg, 2000.0) << "Average latency too high: " << avg << " us"; // 2ms upper bound
    // Max might spike, just print (not asserting strictly yet)
    std::cout << "Latency samples: count=" << samples_us.size() << " avg(us)=" << avg << " max(us)=" << maxv << "\n";
}

// Health test: when checksum != 0 algorithm id must be 1 (CRC32) currently.
TEST(DataIntegrity, ChecksumAlgorithmConsistency) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_latency.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Alg.Transport");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_alg"; cfg.max_width=16; cfg.max_height=16;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    WorldFrame wf; wf.frame_id=1; wf.heightMap.width=8; wf.heightMap.height=8; wf.heightMap.data.assign(64, 2.f);
    // Provide checksum explicitly.
    wf.checksum = caldera::backend::common::crc32(wf.heightMap.data);
    server.sendWorldFrame(wf);

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Alg.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    bool ok=false; SharedMemoryReader::FrameView fv; auto start=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now() - start < 200ms) {
        auto opt = reader.latest();
        if (opt && opt->frame_id==1) { fv=*opt; ok=true; break; }
        std::this_thread::sleep_for(2ms);
    }
    server.stop();
    shm_unlink(cfg.shm_name.c_str());
    ASSERT_TRUE(ok);
    ASSERT_NE(fv.checksum, 0u);
    EXPECT_EQ(fv.checksum_algorithm, 1u) << "Checksum algorithm id mismatch when checksum present";
}
