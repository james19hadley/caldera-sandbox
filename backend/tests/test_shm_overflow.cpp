#include <gtest/gtest.h>
#include "common/Logger.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SharedMemoryReader.h"
#include "processing/ProcessingManager.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using caldera::backend::common::Logger;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::transport::SharedMemoryReader;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;

TEST(SharedMemory, OverflowDropFrame) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm_overflow.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Transport");
    auto logProc = Logger::instance().get("Test.SHM.Proc");

    SharedMemoryTransportServer::Config cfg; // default name
    cfg.max_width = 8; cfg.max_height = 8; // 64 floats capacity per buffer
    SharedMemoryTransportServer server(logTransport, cfg);
    server.start();

    ProcessingManager pm(logProc, nullptr, 0.001f);
    pm.setWorldFrameCallback([&](const WorldFrame& wf){ server.sendWorldFrame(wf); });

    // First a valid frame
    RawDepthFrame ok; ok.sensorId="Test"; ok.width=8; ok.height=8; ok.data.assign(64, 400); ok.timestamp_ns=1;
    pm.processRawDepthFrame(ok);

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    auto first = reader.latest();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->frame_id, 0u);

    // Now an overflowing frame (width beyond capacity)
    RawDepthFrame big; big.sensorId="Test"; big.width=16; big.height=8; big.data.assign(128, 500); big.timestamp_ns=2;
    pm.processRawDepthFrame(big); // should be dropped

    // Give some time; frame id should remain 0
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto after = reader.latest();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->frame_id, 0u); // unchanged

    server.stop();
    shm_unlink(cfg.shm_name.c_str());
}
