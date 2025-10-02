// Basic black-box test using the new SharedMemoryWorldFrameClient abstraction.
// Spawns the SensorBackend process with shared memory transport and consumes frames.

#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <cstring>
#include <limits.h>
#include <unistd.h>

#include "helpers/TestCalderaClient.h"
#include "transport/SharedMemoryReader.h"
#include "common/Logger.h"

using namespace std::chrono_literals;

namespace {
struct ChildProcess { pid_t pid=-1; ~ChildProcess(){ if(pid>0){ int st=0; if(waitpid(pid,&st,WNOHANG)==0){ kill(pid,SIGTERM); waitpid(pid,&st,0);} } } };

static std::string resolveSensorBackendPath() {
    // 1) Allow explicit override via env
    if (const char* env = std::getenv("CALDERA_SENSOR_BACKEND_PATH")) {
        return std::string(env);
    }
    // 2) Derive from the test binary location (/proc/self/exe -> .../backend/build/tests/CalderaTests)
    char exeBuf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf)-1);
    if (n > 0) {
        exeBuf[n] = '\0';
        std::string exePath(exeBuf);
        auto pos = exePath.find_last_of('/');
        std::string exeDir = (pos == std::string::npos) ? std::string(".") : exePath.substr(0, pos);
        // Expected: exeDir == .../backend/build/tests -> candidate is one level up
        std::string candidate = exeDir + "/../SensorBackend";
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        // Another fallback: maybe running from repo root; try cwd + ./backend/build/SensorBackend
        char cwdBuf[PATH_MAX];
        if (::getcwd(cwdBuf, sizeof(cwdBuf))) {
            std::string fromCwd = std::string(cwdBuf) + "/build/SensorBackend";
            if (::access(fromCwd.c_str(), X_OK) == 0) return fromCwd;
            std::string alt = std::string(cwdBuf) + "/backend/build/SensorBackend";
            if (::access(alt.c_str(), X_OK) == 0) return alt;
        }
    }
    // Last resort: original relative path used by test.sh
    return std::string("./build/SensorBackend");
}
}

TEST(ProcessBlackBox, WorldFrameClientSharedMemoryBasic) {
    auto &L = caldera::backend::common::Logger::instance();
    if (!L.isInitialized()) L.initialize("logs/test/process_blackbox.log");
    const std::string exePath = resolveSensorBackendPath();
    const char* exe = exePath.c_str();
    // Use a unique shm name per run to avoid attaching to a stale object from prior runs.
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::string shmName = std::string("/caldera_proc_client_basic_") + std::to_string(getpid()) + "_" + std::to_string(now_ms);
    // Explicit capacity to match server mapping and avoid magic numbers scattered.
    constexpr uint32_t capW = 640;
    constexpr uint32_t capH = 480;
    const std::string capWStr = std::to_string(capW);
    const std::string capHStr = std::to_string(capH);
    ChildProcess child; child.pid = fork();
    ASSERT_NE(child.pid, -1) << "fork failed";
    if (child.pid == 0) {
        setenv("CALDERA_TRANSPORT", "shm", 1);
        setenv("CALDERA_SENSOR_TYPE", "synthetic", 1);
        setenv("CALDERA_SHM_NAME", shmName.c_str(), 1);
        setenv("CALDERA_RUN_SECS", "4", 1);
    // Make server SHM capacity explicit to match client.
        setenv("CALDERA_SHM_MAX_WIDTH", capWStr.c_str(), 1);
        setenv("CALDERA_SHM_MAX_HEIGHT", capHStr.c_str(), 1);
        setenv("CALDERA_LOG_FRAME_TRACE_EVERY", "0", 1);
        setenv("CALDERA_COMPACT_FRAME_LOG", "1", 1);
        setenv("CALDERA_LOG_LEVEL", "info", 1);
        execl(exe, exe, (char*)nullptr);
        _exit(127);
    }
    auto clientLogger = L.get("ProcessBlackBox.Client");
    TestCalderaClient client(clientLogger);
    // Preflight: wait until SHM is actually created/mapped by the child (avoid race)
    {
        caldera::backend::transport::SharedMemoryReader probe(clientLogger);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        bool opened = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (probe.open(shmName, capW, capH)) { opened = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ASSERT_TRUE(opened) << "SHM not created by child within timeout";
        probe.close();
    }
    ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{shmName,capW,capH,true,5000})) << "Client failed to connect to shm";
    ASSERT_TRUE(client.waitForDistinctFrames(3, 4000));
    auto s = client.stats();
    EXPECT_GE(s.distinct_frames, 3u);
    // Cleanup child
    int status=0; kill(child.pid,SIGTERM); waitpid(child.pid,&status,0);
    ASSERT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
}
