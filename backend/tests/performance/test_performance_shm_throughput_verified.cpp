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
#include "helpers/TestCalderaClient.h"

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
    auto logReport = Logger::instance().get("Test.SHM.Bench.Verified.Report");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_bench_verified"; cfg.max_width=256; cfg.max_height=256; cfg.checksum_interval_ms=0;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    // Unified test client for data-plane with checksum verification enabled
    TestCalderaClient client(Logger::instance().get("Test.SHM.Bench.Verified.Client"));
    ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{cfg.shm_name, cfg.max_width, cfg.max_height, true, 2000})) << "Failed to connect client to SHM";

    WorldFrame wf; wf.heightMap.width=256; wf.heightMap.height=256; wf.heightMap.data.resize(256*256);
    for (size_t i=0;i<wf.heightMap.data.size(); ++i) wf.heightMap.data[i] = static_cast<float>(i & 0xFFFF);

    const int iterations = 500; // ~0.5 * 256*256*4 bytes ~ 128MB
    std::atomic<uint64_t> last_seen_id{std::numeric_limits<uint64_t>::max()};
    std::atomic<bool> stop{false};

    std::thread consumer([&]{
        // Быстрый поллинг для захвата как можно большего числа уникальных кадров
        const auto poll_sleep = 200us;
        uint64_t last_id = UINT64_MAX;
        int stable_spins = 0;
        while(!stop.load()) {
            auto opt = client.latest();
            if (opt) {
                if (opt->frame_id != last_id) {
                    last_id = opt->frame_id; stable_spins = 0;
                    last_seen_id.store(last_id, std::memory_order_relaxed);
                } else {
                    if (++stable_spins > 5 && last_id + 1 >= static_cast<uint64_t>(iterations)) {
                        break; // видимо, дошли до конца
                    }
                }
            }
            std::this_thread::sleep_for(poll_sleep);
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
    auto s = client.stats();
    auto lat = client.latencyStats();
    uint64_t observed_last = last_seen_id.load();
    double totalMB = iterations * (256.0*256.0*sizeof(float)) / (1024.0*1024.0);
    double mbps = totalMB / seconds;
    double fps = iterations / seconds;
    double coverage = static_cast<double>(s.distinct_frames) / iterations;
    // Отчет в стиле throughput-интеграций
    logReport->info("Phase7 bench published={} expected={} observed={} coverage={:.2f} mbps={:.2f} fps={:.2f}",
                    stats.frames_published, iterations, s.distinct_frames, coverage, mbps, fps);
    logReport->info("Client stats: distinct={} observed={} max_gap={} skipped={} checksum_present={} verified={} mismatch={} latency_count={} mean_ms={:.3f} p95_ms={:.3f} max_ms={:.3f}",
                    s.distinct_frames, s.frames_observed, s.max_gap, s.total_skipped,
                    s.checksum_present, s.checksum_verified, s.checksum_mismatch,
                    s.latency_samples, lat.mean_ms, lat.p95_ms, lat.max_ms);

    // Invariant assertions (Variant A):
    // 1. Writer published exactly 'iterations'.
    EXPECT_EQ(stats.frames_published, static_cast<uint64_t>(iterations));
    // 2. No capacity drops expected for pure overwrite-double-buffer model.
    EXPECT_EQ(stats.frames_dropped_capacity, 0u);
    // 3. We must have observed at least the final frame id (or very close). Allow off-by-1 in case stop races.
    EXPECT_GE(observed_last, static_cast<uint64_t>(iterations - 2));
    // 4. We require an absolute minimum of verified frames (CRC checks) to ensure integrity exercised.
    //    Empirically we get 370–390 after poll tuning; set a conservative floor of 200.
    // 4. Минимум верифицированных кадров (через общий клиент с включенной проверкой)
    EXPECT_GE(s.checksum_verified, 200u);
    // 5. Консервативный порог покрытия 40% (стабильно под CI вариативностью)
    EXPECT_GE(coverage, 0.40) << "Coverage unexpectedly low (" << (coverage*100.0) << "%)";
}
