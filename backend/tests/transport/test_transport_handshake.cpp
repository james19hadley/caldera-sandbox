#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "TestLocalTransportClient.h"

#include "common/Logger.h"
#include "transport/LocalTransportServer.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::transport::LocalTransportServer;

TEST(Handshake, SuccessfulConnection) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/handshake.log");
        Logger::instance().setGlobalLevel(spdlog::level::info);
    }
    auto srvLog = Logger::instance().get("Test.Handshake.Server");
    auto hsLog  = Logger::instance().get("Test.Handshake.Trace");

    LocalTransportServer::Config cfg; cfg.pipe_s2c="/tmp/caldera_s2c_hs"; cfg.pipe_c2s="/tmp/caldera_c2s_hs"; cfg.server_stats_interval_ms = 0;
    auto server = std::make_shared<LocalTransportServer>(srvLog, hsLog, cfg);
    server->start();
    std::this_thread::sleep_for(30ms);
    TestLocalTransportClient client(Logger::instance().get("Test.Handshake.Client"));
    ASSERT_TRUE(client.handshake({cfg.pipe_s2c, cfg.pipe_c2s})) << "Handshake helper failed";
    // Ensure server sets flag shortly after
    auto deadline_flag = std::chrono::steady_clock::now() + 1s;
    while(!server->isHandshakeComplete() && std::chrono::steady_clock::now() < deadline_flag) std::this_thread::sleep_for(10ms);
    ASSERT_TRUE(server->isHandshakeComplete());
    auto json = client.handshakeJson();
    ASSERT_FALSE(json.empty()) << "Handshake JSON empty";
    client.closeAll();
    server->stop();
    // crude field checks (no JSON parser dependency added yet)
    EXPECT_NE(json.find("protocol_version"), std::string::npos);
    EXPECT_NE(json.find("shm_name_a"), std::string::npos);
    EXPECT_NE(json.find("shm_name_b"), std::string::npos);
    EXPECT_NE(json.find("shm_size"), std::string::npos);
    EXPECT_NE(json.find("height_map_width"), std::string::npos);
    EXPECT_NE(json.find("height_map_height"), std::string::npos);
}

TEST(Handshake, FailsOnBadHello) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/handshake.log");
        Logger::instance().setGlobalLevel(spdlog::level::info);
    }
    auto srvLog = Logger::instance().get("Test.Handshake.Server.Bad");
    auto hsLog  = Logger::instance().get("Test.Handshake.Trace.Bad");
    LocalTransportServer::Config cfg; cfg.pipe_s2c="/tmp/caldera_s2c_hs_bad"; cfg.pipe_c2s="/tmp/caldera_c2s_hs_bad"; cfg.server_stats_interval_ms=0;
    auto server = std::make_shared<LocalTransportServer>(srvLog, hsLog, cfg);
    server->start();
    std::this_thread::sleep_for(30ms);
    TestLocalTransportClient client(Logger::instance().get("Test.Handshake.Client.Bad"));
    // Intentionally wrong greeting
    bool ok = client.handshake({cfg.pipe_s2c, cfg.pipe_c2s}, 500, "HELLO_WRONG_PROTOCOL\n");
    EXPECT_FALSE(ok) << "Handshake should fail with bad HELLO";
    // Give server time to process and terminate its loop
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(server->isHandshakeComplete());
    client.closeAll();
    server->stop();
}
