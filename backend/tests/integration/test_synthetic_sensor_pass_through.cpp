// Integration: Synthetic sensor end-to-end pass-through (formerly Phase 0)
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <unordered_map>
#include "helpers/DeterministicEnvGuard.h"

#include "IntegrationHarness.h"
#include "helpers/TestCalderaClient.h"
#include "common/Checksum.h"
// We'll probe for physical sensors at runtime by invoking the built SensorViewer --list-json
// This avoids linking tools/viewer into the test binary.

using namespace std::chrono_literals;
using caldera::backend::tests::IntegrationHarness;
using caldera::backend::tests::HarnessConfig;
using caldera::backend::hal::SyntheticSensorDevice;
using caldera::backend::common::crc32;

// Helper: regenerate ramp pattern (uint16_t -> then scaled by ProcessingManager scale during pipeline).
static void regenerateRamp(int w, int h, std::vector<uint16_t>& out) {
    out.resize(static_cast<size_t>(w) * h);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) out[static_cast<size_t>(y)*w + x] = static_cast<uint16_t>(x + y);
}

// Compute CRC over float buffer bytes (stable representation assumption: IEEE 754, little-endian typical CI/Linux).
static uint32_t crcFloats(const std::vector<float>& data) { return crc32(data); }

TEST(SyntheticSensorPipeline, SingleSensorPassThroughRamp) {
    // Adaptive integration test: default runs synthetic harness path, but if
    // CALDERA_INTEGRATION_MODE=real the test will attempt to run the real
    // SensorBackend in a subprocess and will skip if no physical sensor is found.
    auto mode_env = std::getenv("CALDERA_INTEGRATION_MODE");
    std::string mode = mode_env ? std::string(mode_env) : std::string("synthetic");

    // Common expected pattern (ramp) and CRC used by both synthetic and real path
    SyntheticSensorDevice::Config sc; sc.width=16; sc.height=16; sc.fps=30.0; sc.pattern=SyntheticSensorDevice::Pattern::RAMP; sc.sensorId="SynthA";
    std::vector<uint16_t> expectedPattern; regenerateRamp(sc.width, sc.height, expectedPattern);
    const float scale = 0.001f; std::vector<float> expectedScaled(expectedPattern.size());
    for (size_t i=0;i<expectedPattern.size();++i) expectedScaled[i] = static_cast<float>(expectedPattern[i]) * scale;
    uint32_t expectedCRC = crcFloats(expectedScaled);

    if (mode == "real") {
        // Real-sensor path: attempt auto-detect using the SensorViewer helper (built executable).
        auto resolveViewerPath = []() -> std::string {
            // Try common locations relative to our binary
            char exeBuf[PATH_MAX];
            ssize_t n = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf)-1);
            std::string candidate = "./build/SensorViewer";
            if (n > 0) {
                exeBuf[n] = '\0';
                std::string exeP(exeBuf);
                auto pos = exeP.find_last_of('/');
                std::string exeDir = (pos == std::string::npos) ? std::string(".") : exeP.substr(0, pos);
                std::string alt = exeDir + "/../SensorViewer";
                if (::access(alt.c_str(), X_OK) == 0) return alt;
            }
            if (::access(candidate.c_str(), X_OK) == 0) return candidate;
            return std::string();
        };

        std::string viewer = resolveViewerPath();
        if (viewer.empty()) {
            GTEST_SKIP() << "SensorViewer executable not found; cannot probe physical sensors; skipping real-sensor test";
        }
        // Run SensorViewer --list-json and inspect stdout
        std::string cmd = viewer + " --list-json 2>/dev/null";
        std::array<char, 8192> buf{};
        std::string out;
        FILE* f = popen(cmd.c_str(), "r");
        if (!f) {
            GTEST_SKIP() << "Failed to run SensorViewer to probe sensors";
        }
        while (fgets(buf.data(), static_cast<int>(buf.size()), f)) out += buf.data();
        int rc = pclose(f);
        // If output is empty or equals [] then no sensors
        if (out.empty() || out.find("[]") != std::string::npos) {
            GTEST_SKIP() << "No physical sensors detected by SensorViewer; skipping real-sensor integration test";
        }

        // Launch SensorBackend as an external process configured to use shm transport
        // and to select auto-detected sensor via default behavior.
        // Reuse pattern from ProcessBlackBox tests for resolving path and exec.
        // Resolve executable path
        char exeBuf[PATH_MAX];
        ssize_t n = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf)-1);
        std::string exePath = "./build/SensorBackend";
        if (n > 0) {
            exeBuf[n] = '\0';
            std::string exeP(exeBuf);
            auto pos = exeP.find_last_of('/');
            std::string exeDir = (pos == std::string::npos) ? std::string(".") : exeP.substr(0, pos);
            std::string candidate = exeDir + "/../SensorBackend";
            if (::access(candidate.c_str(), X_OK) == 0) exePath = candidate;
        }
        if (::access(exePath.c_str(), X_OK) != 0) {
            GTEST_SKIP() << "SensorBackend executable not found at " << exePath << "; build it or set CALDERA_SENSOR_BACKEND_PATH";
        }

        const std::string shmName = "/caldera_integration_real_pass";
        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "fork failed";
        if (pid == 0) {
            // Child: configure backend to use SHM and run with default sensor auto-detect
            setenv("CALDERA_TRANSPORT", "shm", 1);
            setenv("CALDERA_SHM_NAME", shmName.c_str(), 1);
            // Run a short window suitable for CI
            setenv("CALDERA_RUN_SECS", "4", 1);
            // Reduce noise from per-frame logs
            setenv("CALDERA_LOG_FRAME_TRACE_EVERY", "0", 1);
            setenv("CALDERA_COMPACT_FRAME_LOG", "1", 1);
            setenv("CALDERA_LOG_LEVEL", "info", 1);
            execl(exePath.c_str(), exePath.c_str(), (char*)nullptr);
            _exit(127);
        }

        // Parent: attach TestCalderaClient
        auto readerLog = caldera::backend::common::Logger::instance().get("Integration.Reader");
        TestCalderaClient client(readerLog);
        ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{shmName, static_cast<uint32_t>(32), static_cast<uint32_t>(32), true, 3000})) << "Failed to connect data to real backend";

        const int targetFrames = 8;
        int collected = 0; uint64_t lastFrameId = std::numeric_limits<uint64_t>::max();
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (collected < targetFrames && std::chrono::steady_clock::now() < deadline) {
            auto fv = client.latest();
            if (fv && fv->frame_id != lastFrameId) {
                lastFrameId = fv->frame_id;
                ASSERT_EQ(fv->width, sc.width);
                ASSERT_EQ(fv->height, sc.height);
                ASSERT_EQ(fv->float_count, expectedScaled.size());
                uint32_t gotCRC = crc32(fv->data, fv->float_count);
                EXPECT_EQ(gotCRC, expectedCRC) << "CRC mismatch at frame_id=" << fv->frame_id;
                ++collected;
            }
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_EQ(collected, targetFrames) << "Did not collect enough frames from real backend";

        // Cleanup: terminate child process if still running
        int status = 0; if (waitpid(pid, &status, WNOHANG) == 0) { kill(pid, SIGTERM); waitpid(pid, &status, 0); }
        EXPECT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
    } else {
        // Default synthetic path (previous harness behavior)
        auto guard = caldera::backend::tests::EnvVarGuard::disableSpatialAndAdaptive();
        IntegrationHarness harness;
        harness.addSyntheticSensor(sc);
        HarnessConfig hc; hc.shm_name = "/caldera_integration_synth_pass"; hc.max_width=32; hc.max_height=32;
        ASSERT_TRUE(harness.start(hc));

        auto readerLog = caldera::backend::common::Logger::instance().get("Integration.Reader");
        TestCalderaClient client(readerLog);
        ASSERT_TRUE(client.connectData(TestCalderaClient::ShmDataConfig{hc.shm_name, static_cast<uint32_t>(hc.max_width), static_cast<uint32_t>(hc.max_height), true, 2000})) << "Failed to connect data";

        const int targetFrames = 10;
        int collected = 0;
        uint64_t lastFrameId = std::numeric_limits<uint64_t>::max();

        auto deadline = std::chrono::steady_clock::now() + 3s;
        while (collected < targetFrames && std::chrono::steady_clock::now() < deadline) {
            auto fv = client.latest();
            if (fv && fv->frame_id != lastFrameId) {
                lastFrameId = fv->frame_id;
                ASSERT_EQ(fv->width, sc.width);
                ASSERT_EQ(fv->height, sc.height);
                ASSERT_EQ(fv->float_count, expectedScaled.size());
                uint32_t gotCRC = crc32(fv->data, fv->float_count);
                EXPECT_EQ(gotCRC, expectedCRC) << "CRC mismatch at frame_id=" << fv->frame_id;
                ++collected;
            }
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_EQ(collected, targetFrames) << "Did not collect enough frames";
        EXPECT_GE(harness.framesPublished(), static_cast<uint64_t>(targetFrames));

        harness.stop();
    }
}
