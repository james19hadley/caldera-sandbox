// Phase 8: Protocol / Versioning Integration
// Goals:
// 1. Mid-stream reader attachment should observe a near-tip frame (not a stale early frame).
// 2. Reader rejects shared memory segment if header version is tampered.

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "IntegrationHarness.h"
#include "transport/SharedMemoryReader.h"
#include "transport/SharedMemoryLayout.h"
#include "common/Logger.h"

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::transport::SharedMemoryReader;
namespace shmns = caldera::backend::transport::shm;

// Helper to tamper version field.
static void corruptVersion(const std::string& shm_name, uint32_t bad_version) {
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0600);
    ASSERT_NE(fd, -1) << "Failed to open shm segment for corruption";
    void* addr = mmap(nullptr, sizeof(shmns::ShmHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED) << "mmap failed";
    auto* hdr = reinterpret_cast<shmns::ShmHeader*>(addr);
    hdr->version = bad_version;
    msync(addr, sizeof(shmns::ShmHeader), MS_SYNC);
    munmap(addr, sizeof(shmns::ShmHeader));
    close(fd);
}

TEST(TransportMidStreamAttach, ReaderSeesRecentFrame) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/transport_midstream.log");
    }
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.width=32; sc.height=32; sc.fps=30.0; sc.sensorId="MidStreamSensor"; sc.pattern=SyntheticSensorDevice::Pattern::RAMP;
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name="/caldera_phase8_midstream"; hc.max_width=64; hc.max_height=64; hc.processing_scale=0.001f;
    ASSERT_TRUE(harness.start(hc));

    // Let the pipeline publish some frames before attaching reader.
    std::this_thread::sleep_for(500ms); // ~15 frames @30 FPS
    uint64_t published_before = harness.framesPublished();
    ASSERT_GT(published_before, 5u);

    SharedMemoryReader reader(caldera::backend::common::Logger::instance().get("Test.MidStream.Reader"));
    ASSERT_TRUE(reader.open(hc.shm_name, hc.max_width, hc.max_height));

    // First frame we observe should be near the tip. Allow slack of a few frames due to race.
    uint64_t first_id = 0;
    auto deadline = std::chrono::steady_clock::now() + 1s;
    bool got=false;
    uint64_t spins=0;
    while(std::chrono::steady_clock::now() < deadline) {
        auto opt = reader.latest();
        if (opt) { first_id = opt->frame_id; got=true; break; }
        std::this_thread::sleep_for(5ms); ++spins;
    }
    ASSERT_TRUE(got) << "Failed to obtain any frame after attaching reader";
    uint64_t published_after = harness.framesPublished();
    // We expect first_id to be close to current published_after (within 5) OR at least >= published_before - 1
    // because we attached mid-stream and only the latest buffer is observable.
    EXPECT_GE(first_id + 5, published_after) << "First observed frame is too far behind tip";
    EXPECT_GE(first_id, published_before - 5) << "First observed frame unexpectedly far behind pre-attach published count";

    harness.stop();
}

TEST(TransportMidStreamAttach, VersionMismatchRejected) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/transport_midstream.log");
    }
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.sensorId="VersionSensor"; sc.fps=15.0; sc.width=16; sc.height=16; sc.pattern=SyntheticSensorDevice::Pattern::CONSTANT;
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name="/caldera_phase8_version"; hc.max_width=32; hc.max_height=32;
    ASSERT_TRUE(harness.start(hc));
    // Ensure header established
    std::this_thread::sleep_for(100ms);

    // Corrupt version field
    corruptVersion(hc.shm_name, 9999u);

    SharedMemoryReader badReader(caldera::backend::common::Logger::instance().get("Test.MidStream.BadReader"));
    // Opening should fail due to version mismatch
    EXPECT_FALSE(badReader.open(hc.shm_name, hc.max_width, hc.max_height));

    harness.stop();
}

// Reconnect mid-stream: attach, read a frame, close, wait, re-open, read a near-tip frame again.
TEST(TransportMidStreamAttach, ReaderReconnectMidStream) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/transport_midstream.log");
    }
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.sensorId="ReconnectSensor"; sc.fps=45.0; sc.width=24; sc.height=24; sc.pattern=SyntheticSensorDevice::Pattern::STRIPES;
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name="/caldera_phase8_reconnect"; hc.max_width=48; hc.max_height=48; hc.processing_scale=0.001f;
    ASSERT_TRUE(harness.start(hc));
    std::this_thread::sleep_for(300ms); // warm-up
    uint64_t published_before_first = harness.framesPublished();
    ASSERT_GT(published_before_first, 5u);
    {
        SharedMemoryReader reader(caldera::backend::common::Logger::instance().get("Test.MidStream.Reconnect1"));
        ASSERT_TRUE(reader.open(hc.shm_name, hc.max_width, hc.max_height));
        auto first = reader.latest();
        ASSERT_TRUE(first.has_value());
        uint64_t fid1 = first->frame_id;
        EXPECT_GE(fid1, published_before_first - 5);
        reader.close();
    }
    // More frames accumulate
    std::this_thread::sleep_for(300ms);
    uint64_t published_before_second = harness.framesPublished();
    ASSERT_GT(published_before_second, published_before_first + 5);
    {
        SharedMemoryReader reader2(caldera::backend::common::Logger::instance().get("Test.MidStream.Reconnect2"));
        ASSERT_TRUE(reader2.open(hc.shm_name, hc.max_width, hc.max_height));
        auto second = reader2.latest();
        ASSERT_TRUE(second.has_value());
        uint64_t fid2 = second->frame_id;
        EXPECT_GE(fid2, published_before_second - 5);
        EXPECT_GT(fid2, published_before_first); // ensure progress
    }
    harness.stop();
}

// Magic corruption: corrupt magic after start; reader open should fail.
TEST(TransportMidStreamAttach, MagicCorruptionRejected) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/transport_midstream.log");
    }
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.sensorId="MagicSensor"; sc.fps=20.0; sc.width=16; sc.height=16; sc.pattern=SyntheticSensorDevice::Pattern::CHECKER;
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name="/caldera_phase8_magic"; hc.max_width=32; hc.max_height=32;
    ASSERT_TRUE(harness.start(hc));
    std::this_thread::sleep_for(120ms);
    // Corrupt magic
    int fd = shm_open(hc.shm_name.c_str(), O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    void* addr = mmap(nullptr, sizeof(shmns::ShmHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    auto* hdr = reinterpret_cast<shmns::ShmHeader*>(addr);
    hdr->magic = 0x0BADF00D; // invalid magic
    msync(addr, sizeof(shmns::ShmHeader), MS_SYNC);
    munmap(addr, sizeof(shmns::ShmHeader));
    close(fd);
    SharedMemoryReader reader(caldera::backend::common::Logger::instance().get("Test.MidStream.Magic"));
    EXPECT_FALSE(reader.open(hc.shm_name, hc.max_width, hc.max_height)) << "Reader should reject corrupted magic";
    harness.stop();
}

// Live version flip: open reader successfully, then corrupt version; subsequent latest() should return nullopt or stable frames but not crash.
TEST(TransportMidStreamAttach, LiveVersionFlipGraceful) {
    if (!caldera::backend::common::Logger::instance().isInitialized()) {
        caldera::backend::common::Logger::instance().initialize("logs/test/transport_midstream.log");
    }
    IntegrationHarness harness;
    SyntheticSensorDevice::Config sc; sc.sensorId="FlipSensor"; sc.fps=25.0; sc.width=20; sc.height=20; sc.pattern=SyntheticSensorDevice::Pattern::RADIAL;
    harness.addSyntheticSensor(sc);
    HarnessConfig hc; hc.shm_name="/caldera_phase8_flip"; hc.max_width=40; hc.max_height=40;
    ASSERT_TRUE(harness.start(hc));
    std::this_thread::sleep_for(150ms);
    SharedMemoryReader reader(caldera::backend::common::Logger::instance().get("Test.MidStream.Flip"));
    ASSERT_TRUE(reader.open(hc.shm_name, hc.max_width, hc.max_height));
    auto before = reader.latest();
    ASSERT_TRUE(before.has_value());
    // Corrupt version in-place
    int fd = shm_open(hc.shm_name.c_str(), O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    void* addr = mmap(nullptr, sizeof(shmns::ShmHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    auto* hdr = reinterpret_cast<shmns::ShmHeader*>(addr);
    hdr->version = 424242; // bogus
    msync(addr, sizeof(shmns::ShmHeader), MS_SYNC);
    munmap(addr, sizeof(shmns::ShmHeader));
    close(fd);
    // Reader 'latest' behavior is unspecified after corruption; we just ensure no crash and either null or stale frame.
    auto after = reader.latest();
    // Sanity: should not segfault / UB; allow any outcome (we log). If still returning a frame, frame_id monotonicity may hold.
    (void)after;
    harness.stop();
}
