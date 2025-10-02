#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/Logger.h"
#include "transport/SharedMemoryTransportServer.h"
#include "processing/ProcessingManager.h"

using caldera::backend::common::Logger;
using caldera::backend::transport::SharedMemoryTransportServer;
using caldera::backend::processing::ProcessingManager;
using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::WorldFrame;

#include "transport/SharedMemoryReader.h"
using caldera::backend::transport::SharedMemoryReader;

TEST(SharedMemory, WriterReaderBasic) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/shm.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logTransport = Logger::instance().get("Test.SHM.Transport");
    auto logProc = Logger::instance().get("Test.SHM.Proc");

    SharedMemoryTransportServer::Config cfg; // default /caldera_worldframe
    cfg.max_width = 32; cfg.max_height = 32;
    SharedMemoryTransportServer server(logTransport, cfg);
    server.start();

    // Prepare a processing manager to feed a frame to the server manually via callback wiring
    ProcessingManager pm(logProc, nullptr, 0.001f);
    std::atomic<bool> gotFrame{false};
    // Replace PM callback with lambda that uses server directly
    pm.setWorldFrameCallback([&](const WorldFrame& wf){ server.sendWorldFrame(wf); gotFrame = true; });

    // produce several frames to exercise buffer flip
    for (int f=0; f<3; ++f) {
        RawDepthFrame raw; raw.sensorId="Test"; raw.width=8; raw.height=4; raw.data.assign(32, static_cast<uint16_t>(500+f)); raw.timestamp_ns = 42+f;
        pm.processRawDepthFrame(raw);
    }
    ASSERT_TRUE(gotFrame.load());

    SharedMemoryReader reader(Logger::instance().get("Test.SHM.Reader"));
    ASSERT_TRUE(reader.open(cfg.shm_name, cfg.max_width, cfg.max_height));
    // poll for latest
    SharedMemoryReader::FrameView fv; bool have=false;
    for (int i=0;i<50 && !have; ++i) {
        auto opt = reader.latest();
        if (opt) { fv = *opt; have=true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(have);
    EXPECT_EQ(fv.frame_id, 2u); // last of 3 frames (0,1,2)
    EXPECT_EQ(fv.width, 8u);
    EXPECT_EQ(fv.height, 4u);
    EXPECT_EQ(fv.float_count, 32u);
    // sample a couple of float values (scale: depth 500+f * 0.001f)
    ASSERT_NE(fv.data, nullptr);
    EXPECT_NEAR(fv.data[0], (500+2)*0.001f, 1e-6);

    server.stop();
    shm_unlink(cfg.shm_name.c_str());
}
