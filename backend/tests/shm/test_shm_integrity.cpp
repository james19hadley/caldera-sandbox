#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "common/Logger.h"
#include "common/DataTypes.h"
#include "common/Checksum.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::common::WorldFrame;
using caldera::backend::common::crc32;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;

TEST(DataIntegrity, ChecksumValidation) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_integrity.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Int.Transport");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_integrity"; cfg.max_width=64; cfg.max_height=64;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    // Construct frame
    WorldFrame wf; wf.frame_id = 1; wf.timestamp_ns = 1234567890ULL;
    wf.heightMap.width = 32; wf.heightMap.height = 16;
    wf.heightMap.data.resize(wf.heightMap.width * wf.heightMap.height);
    for (int i=0;i<wf.heightMap.width * wf.heightMap.height;++i) {
        wf.heightMap.data[i] = static_cast<float>((i % 4) + 1); // 1,2,3,4 repeating
    }
    wf.checksum = crc32(wf.heightMap.data);
    server.sendWorldFrame(wf);

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Int.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    bool got=false; SharedMemoryReader::FrameView fv; 
    for (int i=0;i<50 && !got; ++i) {
        auto opt = reader.latest();
        if (opt) { fv=*opt; got=true; break; }
        std::this_thread::sleep_for(5ms);
    }
    ASSERT_TRUE(got);
    ASSERT_EQ(fv.frame_id, 1u);
    ASSERT_EQ(fv.float_count, wf.heightMap.data.size());
    // compute checksum of received floats
    uint32_t recv_crc = crc32(fv.data, fv.float_count);
    EXPECT_EQ(recv_crc, wf.checksum);
    EXPECT_EQ(fv.checksum, wf.checksum);
    // spot check some elements
    EXPECT_NEAR(fv.data[0], 1.f, 1e-6);
    EXPECT_NEAR(fv.data[1], 2.f, 1e-6);
    EXPECT_NEAR(fv.data[2], 3.f, 1e-6);
    EXPECT_NEAR(fv.data[3], 4.f, 1e-6);

    server.stop();
    shm_unlink(cfg.shm_name.c_str());
}

TEST(DataIntegrity, ChecksumMismatchDetection) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_integrity.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Int.Transport2");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name = "/caldera_worldframe_integrity_bad"; cfg.max_width=32; cfg.max_height=32;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();

    WorldFrame wf; wf.frame_id=7; wf.heightMap.width=8; wf.heightMap.height=8; wf.heightMap.data.resize(64, 1.f);
    wf.checksum = crc32(wf.heightMap.data); // correct
    server.sendWorldFrame(wf);

    // Now modify data and send again with stale checksum to simulate corruption
    wf.frame_id=8; wf.heightMap.data[10] = 999.f; // mutate one value
    // Intentionally DO NOT recompute wf.checksum -> mismatch scenario
    server.sendWorldFrame(wf);

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Int.Reader2"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    bool got=false; SharedMemoryReader::FrameView fv;
    for (int i=0;i<50 && !got; ++i) {
        auto opt = reader.latest();
        if (opt) { fv=*opt; got=true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(got);
    ASSERT_EQ(fv.frame_id, 8u);
    uint32_t recomputed = crc32(fv.data, fv.float_count);
    // Expect mismatch due to intentional corruption
    EXPECT_NE(recomputed, fv.checksum);
    server.stop();
    shm_unlink(cfg.shm_name.c_str());
}
