#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include "helpers/TestCalderaClient.h"
#include "common/Logger.h"

using namespace std::chrono_literals;
using caldera::backend::common::Logger;

static std::string uniqueSockPath(const char* tag){
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return std::string("/tmp/") + tag + std::to_string(getpid()) + "_" + std::to_string(now_ms) + ".sock";
}

static std::string resolveSensorBackendPath() {
    if (const char* env = std::getenv("CALDERA_SENSOR_BACKEND_PATH")) {
        return std::string(env);
    }
    char exeBuf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf)-1);
    if (n > 0) {
        exeBuf[n] = '\0';
        std::string exePath(exeBuf);
        auto pos = exePath.find_last_of('/');
        std::string exeDir = (pos == std::string::npos) ? std::string(".") : exePath.substr(0, pos);
        std::string candidate = exeDir + "/../SensorBackend";
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        char cwdBuf[PATH_MAX];
        if (::getcwd(cwdBuf, sizeof(cwdBuf))) {
            std::string fromCwd = std::string(cwdBuf) + "/build/SensorBackend";
            if (::access(fromCwd.c_str(), X_OK) == 0) return fromCwd;
            std::string alt = std::string(cwdBuf) + "/backend/build/SensorBackend";
            if (::access(alt.c_str(), X_OK) == 0) return alt;
        }
    }
    return std::string("./build/SensorBackend");
}

TEST(TransportSocketParity, ShmVsSocket_BasicCoverageAndCRC) {
    auto &L = Logger::instance(); if (!L.isInitialized()) L.initialize("logs/test/socket_parity.log");
    // Unique SHM and socket endpoints per run
    const std::string shmName = std::string("/caldera_socket_parity_") + std::to_string(getpid());
    const std::string sockPath = uniqueSockPath("caldera_udswf_");
    const std::string endpoint = std::string("unix:") + sockPath;

    // Launch backend with socket transport
    pid_t pid = fork(); ASSERT_NE(pid, -1);
    if (pid == 0) {
        const std::string exePath = resolveSensorBackendPath();
        setenv("CALDERA_TRANSPORT", "socket", 1);
        setenv("CALDERA_SENSOR_TYPE", "synthetic", 1);
        setenv("CALDERA_SOCKET_ENDPOINT", endpoint.c_str(), 1);
        setenv("CALDERA_RUN_SECS", "2", 1);
        execl(exePath.c_str(), exePath.c_str(), (char*)nullptr);
        _exit(127);
    }

    TestCalderaClient client(Logger::instance().get("SocketParity.Client"));
    ASSERT_TRUE(client.connectData(TestCalderaClient::SocketDataConfig{endpoint, true, 3000})) << "socket client connect failed";
    ASSERT_TRUE(client.waitForDistinctFrames(10, 2500));
    auto s = client.stats(); auto lat = client.latencyStats();
    EXPECT_GE(s.distinct_frames, 10u);
    // If checksum present appears (auto-interval), there must be zero mismatches
    EXPECT_EQ(s.checksum_mismatch, 0u);
    if (lat.count > 0) EXPECT_LT(lat.p95_ms, 50.0);

    // Cleanup
    int status=0; waitpid(pid, &status, 0);
    ASSERT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
}
