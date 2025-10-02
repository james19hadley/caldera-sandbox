#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "common/StatsUtil.h"

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
using caldera::backend::common::crc32;

TEST(SharedMemoryBenchmark, ThroughputWithReaderVerification) { // LARGE
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_benchmark_verified.log");
    }
    Logger::instance().setGlobalLevel(spdlog::level::info);
    auto logTransport = Logger::instance().get("Test.SHM.Bench.Verified.Transport");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_bench_verified"; cfg.max_width=256; cfg.max_height=256; cfg.checksum_interval_ms=0;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Bench.Verified.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    WorldFrame wf; wf.heightMap.width=256; wf.heightMap.height=256; wf.heightMap.data.resize(256*256);
    for (size_t i=0;i<wf.heightMap.data.size(); ++i) wf.heightMap.data[i] = static_cast<float>(i & 0xFFFF);

    const int iterations = 500; // ~0.5 * 256*256*4 bytes ~ 128MB
    std::atomic<uint64_t> verified{0};
    std::atomic<bool> stop{false};

    std::thread consumer([&]{
        uint64_t last_id = UINT64_MAX;
        int stable_spins = 0;
        while(!stop.load()) {
            auto opt = reader.latest();
            if (opt) {
                if (opt->frame_id != last_id) {
                    last_id = opt->frame_id; stable_spins = 0;
                    if (opt->checksum != 0) {
                        auto c = crc32(opt->data, opt->float_count);
                        if (c == opt->checksum) verified.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    if (++stable_spins > 5 && last_id + 1 >= static_cast<uint64_t>(iterations)) {
                        // we've likely consumed final frame enough times
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(500us);
        }
    });

    auto start = std::chrono::high_resolution_clock::now();
    for (int i=0;i<iterations; ++i) {
        wf.frame_id = i;
        wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        wf.checksum = crc32(wf.heightMap.data); // explicit
        server.sendWorldFrame(wf);
    }
    // Allow reader extra time to catch up
    std::this_thread::sleep_for(200ms);
    auto end = std::chrono::high_resolution_clock::now();
    stop.store(true); consumer.join();
    auto stats = server.snapshotStats();
    using caldera::backend::common::prettyFps;
    std::cout << "[Benchmark.Verified] published=" << stats.frames_published
              << " dropped=" << stats.frames_dropped_capacity
              << " bytes=" << stats.bytes_written
              << " writer_fps_est=" << prettyFps(stats.last_publish_fps) << "\n";
    server.stop(); shm_unlink(cfg.shm_name.c_str());

    double seconds = std::chrono::duration<double>(end-start).count();
    uint64_t ok = verified.load();
    double totalMB = iterations * (256.0*256.0*sizeof(float)) / (1024.0*1024.0);
    double mbps = totalMB / seconds;
    double fps = iterations / seconds;
    std::cout << "Verified Throughput: " << mbps << " MB/s | Frames/sec=" << fps << " | Verified=" << ok << "/" << iterations << "\n";
    // Expect that the reader has successfully verified majority (ideally all) frames
    EXPECT_GE(ok, static_cast<uint64_t>(iterations * 0.60));
}
