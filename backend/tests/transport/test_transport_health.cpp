#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#include "common/Logger.h"
#include "transport/LocalTransportServer.h"
#include "TestLocalTransportClient.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;
using caldera::backend::transport::LocalTransportServer;

namespace {
// Read just the initial handshake JSON object (multi-line) without blocking forever.
// Stops when '}' encountered or timeout expires.
std::string readHandshakeJson(int fd, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string out; out.reserve(256);
    char buf[128]; ssize_t n;
    while (std::chrono::steady_clock::now() < deadline) {
        n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, buf + n);
            if (out.find('}') != std::string::npos) break; // end of JSON object
            continue;
        } else if (n == 0) {
            // Writer keeps pipe open; brief sleep then retry
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else { // n < 0
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break; // other error
        }
    }
    return out;
}
}

TEST(ClientHealth, StaysAliveWithHeartbeats) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/transport_health.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logSrv = Logger::instance().get("Test.Health.Server");
    auto logHs  = Logger::instance().get("Test.Health.Handshake");
    LocalTransportServer::Config cfg; cfg.pipe_s2c="/tmp/caldera_s2c_health1"; cfg.pipe_c2s="/tmp/caldera_c2s_health1";
    auto server = std::make_shared<LocalTransportServer>(logSrv, logHs, cfg);
    server->start();

    std::this_thread::sleep_for(50ms); // give server time to create pipes

    TestLocalTransportClient client(Logger::instance().get("Test.Health.Client"));
    ASSERT_TRUE(client.handshake({cfg.pipe_s2c, cfg.pipe_c2s})) << "Handshake failed";

    // Heartbeats loop ~5s (every 500ms)
    for (int i=0;i<10;++i) {
        ASSERT_TRUE(client.sendHeartbeat());
        std::this_thread::sleep_for(500ms);
    }
    EXPECT_TRUE(server->isHandshakeComplete());
    EXPECT_TRUE(server->isClientAlive(1s));

    server->stop();
}

TEST(ClientHealth, TimesOutWithoutHeartbeats) {
    if (!Logger::instance().isInitialized()) {
        Logger::instance().initialize("logs/test/transport_health.log");
        Logger::instance().setGlobalLevel(spdlog::level::err);
    }
    auto logSrv = Logger::instance().get("Test.Health.Server2");
    auto logHs  = Logger::instance().get("Test.Health.Handshake2");
    LocalTransportServer::Config cfg; cfg.pipe_s2c="/tmp/caldera_s2c_health2"; cfg.pipe_c2s="/tmp/caldera_c2s_health2";
    auto server = std::make_shared<LocalTransportServer>(logSrv, logHs, cfg);
    server->start();
    std::this_thread::sleep_for(50ms);

    TestLocalTransportClient client(Logger::instance().get("Test.Health.Client2"));
    ASSERT_TRUE(client.handshake({cfg.pipe_s2c, cfg.pipe_c2s})) << "Handshake failed";
    // No heartbeats now; wait 3s
    std::this_thread::sleep_for(3s);
    EXPECT_FALSE(server->isClientAlive(2s));
    server->stop();
}
