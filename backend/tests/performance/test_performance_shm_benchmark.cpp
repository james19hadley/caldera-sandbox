#include <gtest/gtest.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "common/StatsUtil.h"

#include "common/Logger.h"
#include "common/DataTypes.h"
#include "common/Checksum.h"
#include "transport/SharedMemoryTransportServer.h"

using caldera::backend::common::Logger;
using caldera::backend::common::WorldFrame;
using caldera::backend::transport::SharedMemoryTransportServer;

TEST(SharedMemoryBenchmark, Throughput) { // LARGE: performance
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_benchmark.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Bench.Transport");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_bench"; cfg.max_width=512; cfg.max_height=512;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    WorldFrame wf; wf.frame_id = 0; wf.timestamp_ns = 0;
    wf.heightMap.width = 512; wf.heightMap.height = 512;
    wf.heightMap.data.resize(512*512);
    for (size_t i=0;i<wf.heightMap.data.size(); ++i) wf.heightMap.data[i] = static_cast<float>(i & 0xFF);

    const int iterations = 1000; // ~1GB (1000 * 1MB)
    auto start = std::chrono::high_resolution_clock::now();
    auto first_send = start;
    for (int i=0;i<iterations; ++i) {
        wf.frame_id = static_cast<uint64_t>(i);
        auto t0 = std::chrono::high_resolution_clock::now();
        server.sendWorldFrame(wf);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (i==0) first_send = t0;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(end-start).count();
    double avg_latency_us = (std::chrono::duration<double,std::micro>(end - first_send).count()) / iterations; // coarse upper bound
    double totalMB = iterations * (512.0*512.0*sizeof(float)) / (1024.0*1024.0);
    double mbps = totalMB / seconds;
    double fps = iterations / seconds;
    std::cout << "SharedMemory Throughput: " << mbps << " MB/s (" << totalMB << " MB in " << seconds << " s) | FPS=" << fps << " | AvgLatencyApprox(us)=" << avg_latency_us << "\n";

    auto stats = server.snapshotStats();
    using caldera::backend::common::prettyFps;
    std::cout << "[Benchmark.Raw] published=" << stats.frames_published
              << " dropped=" << stats.frames_dropped_capacity
              << " bytes=" << stats.bytes_written
              << " writer_fps_est=" << prettyFps(stats.last_publish_fps) << "\n";
    server.stop();
    shm_unlink(cfg.shm_name.c_str());
}
