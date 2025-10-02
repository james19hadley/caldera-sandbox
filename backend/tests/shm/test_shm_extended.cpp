#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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

// Test periodic checksum interval: with interval, not every frame should recompute checksum.
TEST(SharedMemoryExtended, PeriodicChecksumInterval) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_extended.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Ext.Transport1");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_periodic"; cfg.max_width=16; cfg.max_height=16; cfg.checksum_interval_ms=50; // 50ms
    SharedMemoryTransportServer server(logTransport, cfg); server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Ext.Reader1"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));

    WorldFrame wf; wf.heightMap.width=8; wf.heightMap.height=8; wf.heightMap.data.assign(64, 1.f);
    std::vector<uint32_t> checksums;
    for (int i=0;i<5;++i) {
        wf.frame_id = i;
        wf.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        server.sendWorldFrame(wf);
        std::this_thread::sleep_for(10ms);
        auto opt = reader.latest(); ASSERT_TRUE(opt);
        checksums.push_back(opt->checksum);
    }
    // Expect at least one zero (frames where interval not elapsed) and at least one non-zero.
    bool any_zero=false, any_nonzero=false; for(auto c: checksums){ if(c==0) any_zero=true; else any_nonzero=true; }
    EXPECT_TRUE(any_zero);
    EXPECT_TRUE(any_nonzero);
    server.stop(); shm_unlink(cfg.shm_name.c_str());
}

// Double-buffer race resistance: rapid publishes; reader should never see meta.ready!=1 for active.
TEST(SharedMemoryExtended, DoubleBufferConsistency) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_extended.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Ext.Transport2");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_race"; cfg.max_width=32; cfg.max_height=32;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Ext.Reader2"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    WorldFrame wf; wf.heightMap.width=4; wf.heightMap.height=4; wf.heightMap.data.assign(16, 2.f);
    for (int i=0;i<200;++i) { wf.frame_id=i; wf.timestamp_ns=i; server.sendWorldFrame(wf); auto v=reader.latest(); if(v) { EXPECT_EQ(v->height,4u); } }
    server.stop(); shm_unlink(cfg.shm_name.c_str());
}

// Boundary capacity: exactly fits vs +1 overflow
TEST(SharedMemoryExtended, CapacityBoundary) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_extended.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Ext.Transport3");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_boundary"; cfg.max_width=8; cfg.max_height=8;
    SharedMemoryTransportServer server(logTransport, cfg); server.start();
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Ext.Reader3"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    WorldFrame wf; wf.heightMap.width=8; wf.heightMap.height=8; wf.heightMap.data.assign(64, 3.f); wf.frame_id=1; server.sendWorldFrame(wf);
    auto v1=reader.latest(); ASSERT_TRUE(v1); EXPECT_EQ(v1->frame_id,1u);
    // overflow by +1 width
    wf.frame_id=2; wf.heightMap.width=9; wf.heightMap.height=8; wf.heightMap.data.assign(72,4.f); server.sendWorldFrame(wf);
    std::this_thread::sleep_for(5ms);
    auto v2=reader.latest(); ASSERT_TRUE(v2); EXPECT_EQ(v2->frame_id,1u); // unchanged
    server.stop(); shm_unlink(cfg.shm_name.c_str());
}

// Reader opens before writer starts mapping: expect open failure then success after start.
TEST(SharedMemoryExtended, ReaderBeforeWriter) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_extended.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Ext.Transport4");
    SharedMemoryTransportServer::Config cfg; cfg.shm_name="/caldera_worldframe_early"; cfg.max_width=8; cfg.max_height=8;
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Ext.Reader4"));
    // Should fail open (shm not created yet)
    EXPECT_FALSE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    SharedMemoryTransportServer server(logTransport, cfg); server.start();
    // Now open succeeds
    SharedMemoryReader reader2(Logger::instance().get("Test.SHM.Ext.Reader4b"));
    EXPECT_TRUE(reader2.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    server.stop(); shm_unlink(cfg.shm_name.c_str());
}

// Magic/version mismatch: create a bogus shm and ensure reader rejects.
TEST(SharedMemoryExtended, HeaderMagicVersionMismatch) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_extended.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    const char* name = "/caldera_worldframe_badheader";
    // Create manual shm with wrong magic
    int fd = shm_open(name, O_CREAT|O_RDWR, 0666);
    ASSERT_GE(fd,0);
    size_t sz = 4096; ASSERT_EQ(ftruncate(fd, sz),0);
    void* ptr = mmap(nullptr, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(ptr, MAP_FAILED);
    struct FakeHdr { uint32_t magic; uint32_t version; uint32_t rest[6]; }; // simplistic
    auto* hdr = reinterpret_cast<FakeHdr*>(ptr);
    hdr->magic = 0x11111111; hdr->version = 99; // invalid
    msync(ptr, sizeof(FakeHdr), MS_SYNC);
    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Ext.Reader5"));
    EXPECT_FALSE(reader.open(name, 8, 8));
    munmap(ptr, sz); close(fd); shm_unlink(name);
}
