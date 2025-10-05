// Integration: Real-sensor adaptive end-to-end scaffold
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>

#include "helpers/TestCalderaClient.h"
#include "common/Logger.h"

using namespace std::chrono_literals;

static std::string resolveSensorBackendPath() {
    if (const char* env = std::getenv("CALDERA_SENSOR_BACKEND_PATH")) return std::string(env);
    char exeBuf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf)-1);
    if (n > 0) {
        exeBuf[n] = '\0';
        std::string exePath(exeBuf);
        auto pos = exePath.find_last_of('/');
        std::string exeDir = (pos == std::string::npos) ? std::string(".") : exePath.substr(0, pos);
        std::string candidate = exeDir + "/../SensorBackend";
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        std::string fromCwd = std::string("./build/SensorBackend");
        if (::access(fromCwd.c_str(), X_OK) == 0) return fromCwd;
    }
    return std::string();
}

static std::string resolveSensorViewerPath() {
    // Check environment override first
    if (const char* env = std::getenv("CALDERA_SENSOR_VIEWER_PATH")) {
        if (::access(env, X_OK) == 0) return std::string(env);
    }
    // Common candidate locations relative to CWD or test binary
    const char* candidates[] = {
        "./build/SensorViewer",
        "../SensorViewer",
        "../build/SensorViewer",
        "/home/ging/prog/caldera-sandbox/backend/build/SensorViewer",
    };
    for (auto &c : candidates) {
        if (::access(c, X_OK) == 0) return std::string(c);
    }
    // Try resolving relative to the test binary location
    char exeBuf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf)-1);
    if (n > 0) {
        exeBuf[n] = '\0';
        std::string exePath(exeBuf);
        auto pos = exePath.find_last_of('/');
        std::string exeDir = (pos == std::string::npos) ? std::string(".") : exePath.substr(0, pos);
        std::string candidate = exeDir + "/../SensorViewer";
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return std::string();
}

static std::string resolveDataAnalyzerPath() {
    if (const char* env = std::getenv("CALDERA_DATA_ANALYZER_PATH")) return std::string(env);
    const char* candidates[] = {
        "./build/DataAnalyzer",
        "../DataAnalyzer",
        "/home/ging/prog/caldera-sandbox/backend/build/DataAnalyzer",
    };
    for (auto &c : candidates) if (::access(c, X_OK) == 0) return std::string(c);
    return std::string();
}

TEST(RealSensorE2E, AutoDetectAndCollect) {
    auto &L = caldera::backend::common::Logger::instance();
    if (!L.isInitialized()) L.initialize("logs/test/real_sensor_e2e.log");

    // Probe via SensorViewer --list-json
    std::string viewer = resolveSensorViewerPath();
    if (viewer.empty()) GTEST_SKIP() << "SensorViewer not found; cannot probe physical sensors";
    std::string cmd = viewer + " --list-json 2>/dev/null";
    std::array<char,8192> buf{}; std::string out;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) GTEST_SKIP() << "Failed to run SensorViewer";
    while (fgets(buf.data(), static_cast<int>(buf.size()), f)) out += buf.data();
    int rc = pclose(f);
    if (out.empty() || out.find("[]") != std::string::npos) GTEST_SKIP() << "No physical sensors detected on this host";

    // Basic JSON sniff to pick a sensor type for the backend
    std::string detected_sensor = "mock";
    if (out.find("\"type\":\"KINECT_V1\"") != std::string::npos) detected_sensor = "kinect_v1";
    else if (out.find("\"type\":\"KINECT_V2\"") != std::string::npos) detected_sensor = "kinect_v2";
    // Log detection
    L.get("RealSensorE2E.Probe")->info("Probe output: {} -> selecting sensor type {}", out, detected_sensor);

    // Launch SensorBackend with SHM transport; let backend auto-detect sensor
    auto exe = resolveSensorBackendPath();
    if (exe.empty()) GTEST_SKIP() << "SensorBackend executable not found; build it or set CALDERA_SENSOR_BACKEND_PATH";

    const std::string shmName = "/caldera_real_e2e_shm";
    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "fork failed";
    if (pid == 0) {
        setenv("CALDERA_TRANSPORT", "shm", 1);
        setenv("CALDERA_SHM_NAME", shmName.c_str(), 1);
        setenv("CALDERA_RUN_SECS", "6", 1);
        setenv("CALDERA_LOG_FRAME_TRACE_EVERY", "0", 1);
        setenv("CALDERA_COMPACT_FRAME_LOG", "1", 1);
        setenv("CALDERA_LOG_LEVEL", "info", 1);
        setenv("CALDERA_SENSOR_TYPE", detected_sensor.c_str(), 1);
        execl(exe.c_str(), exe.c_str(), (char*)nullptr);
        _exit(127);
    }

    // Attach TestCalderaClient and collect a few frames
    auto clientLogger = L.get("RealSensorE2E.Client");
    TestCalderaClient client(clientLogger);
    bool connected = client.connectData(TestCalderaClient::ShmDataConfig{shmName, 640, 480, true, 5000});
    if (!connected) {
        // Cleanup child then skip
        kill(pid, SIGTERM); waitpid(pid, nullptr, 0);
        GTEST_SKIP() << "Failed to connect to SHM published by backend";
    }

    // Collect up to N distinct frames
    const int want = 6; int got=0; uint64_t lastId = std::numeric_limits<uint64_t>::max();
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (got < want && std::chrono::steady_clock::now() < deadline) {
        auto fv = client.latest();
        if (fv && fv->frame_id != lastId) { lastId = fv->frame_id; ++got; }
        std::this_thread::sleep_for(10ms);
    }

    // Terminate backend process
    if (waitpid(pid, nullptr, WNOHANG) == 0) { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); }

    EXPECT_GT(got, 0) << "No frames observed from real sensor backend";
    EXPECT_GE(got, 3) << "Insufficient frames collected for validation";

    // Optional: record short raw sensor data and run DataAnalyzer
    const char* runAnalyzerEnv = std::getenv("CALDERA_RUN_ANALYZER");
    if (runAnalyzerEnv && std::string(runAnalyzerEnv) == "1") {
        std::string viewer = resolveSensorViewerPath();
        std::string analyzer = resolveDataAnalyzerPath();
        if (viewer.empty() || analyzer.empty()) {
            L.get("RealSensorE2E.Analyzer")->warn("Analyzer or SensorViewer not found - skipping analyzer step");
        } else {
            std::string tmpFile = std::string("/tmp/caldera_integration_") + std::to_string(getpid()) + ".dat";
            pid_t vid = fork();
            if (vid == 0) {
                // child: run SensorViewer -r tmpFile -t 4 --headless
                execl(viewer.c_str(), viewer.c_str(), (char*)"-r", tmpFile.c_str(), (char*)"-t", (char*)"4", (char*)"--headless", (char*)nullptr);
                _exit(127);
            } else if (vid > 0) {
                int status = 0; waitpid(vid, &status, 0);
                // file should exist
                if (::access(tmpFile.c_str(), R_OK) != 0) {
                    L.get("RealSensorE2E.Analyzer")->warn("Recording failed or file missing: {}", tmpFile);
                } else {
                    // Run DataAnalyzer and time it
                    auto t0 = std::chrono::steady_clock::now();
                    pid_t aid = fork();
                    if (aid == 0) {
                        // Redirect analyzer stdout/stderr into logs/data_analyzer.log so we can parse ANALYSIS_SUMMARY
                        const char* analyzerLogPath = "logs/data_analyzer.log";
                        int fd = ::open(analyzerLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd >= 0) {
                            ::dup2(fd, STDOUT_FILENO);
                            ::dup2(fd, STDERR_FILENO);
                            if (fd > STDERR_FILENO) ::close(fd);
                        }
                        // Request analyzer to write a deterministic sidecar JSON for reliable parsing
                        std::string sidecar = std::string("/tmp/caldera_analysis_summary_") + std::to_string(getpid()) + ".json";
                        setenv("CALDERA_ANALYSIS_SIDECAR", sidecar.c_str(), 1);
                        execl(analyzer.c_str(), analyzer.c_str(), tmpFile.c_str(), (char*)nullptr);
                        _exit(127);
                    } else if (aid > 0) {
                        int ast=0; waitpid(aid, &ast, 0);
                        auto t1 = std::chrono::steady_clock::now();
                        double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t1-t0).count();
                        // Parse analyzer log for processed frames
                        const char* logPath = "logs/data_analyzer.log";
                        int processed = 0;
                        FILE* lf = fopen(logPath, "r");
                        if (lf) {
                            char line[1024];
                            while (fgets(line, sizeof(line), lf)) {
                                if (strstr(line, "Analysis complete. Processed")) {
                                    // find integer
                                    char *p = strstr(line, "Processed");
                                    if (p) {
                                        int v = 0; if (sscanf(p, "Processed %d frames", &v) == 1) processed = v;
                                    }
                                }
                            }
                            fclose(lf);
                        }
                        double fps = (processed > 0 && secs > 0.0) ? (processed / secs) : 0.0;
                        L.get("RealSensorE2E.Analyzer")->info("DataAnalyzer ran in {:.3f}s processed={} fps={:.2f}", secs, processed, fps);
                        // Prefer sidecar JSON written by analyzer for reliable parsing
                        int a_frames = 0; double a_elapsed=0.0, a_fps=0.0, a_mean=0.0, a_stddev=0.0;
                        std::string sidecar = std::string("/tmp/caldera_analysis_summary_") + std::to_string(getpid()) + ".json";
                        if (::access(sidecar.c_str(), R_OK) == 0) {
                            // Read JSON-like sidecar (very small, parse manually)
                            FILE* sf = fopen(sidecar.c_str(), "r");
                            if (sf) {
                                char buf[512]; size_t n = fread(buf, 1, sizeof(buf)-1, sf); buf[n] = '\0'; fclose(sf);
                                // Very small ad-hoc parse: find keys
                                char* p = buf;
                                if ((p = strstr(buf, "\"frames\":"))) a_frames = atoi(p + strlen("\"frames\":"));
                                if ((p = strstr(buf, "\"elapsed_s\":"))) a_elapsed = atof(p + strlen("\"elapsed_s\":"));
                                if ((p = strstr(buf, "\"fps\":"))) a_fps = atof(p + strlen("\"fps\":"));
                                if ((p = strstr(buf, "\"mean_depth\":"))) a_mean = atof(p + strlen("\"mean_depth\":"));
                                if ((p = strstr(buf, "\"stddev_depth\":"))) a_stddev = atof(p + strlen("\"stddev_depth\":"));
                                L.get("RealSensorE2E.Analyzer")->info("Read sidecar {} -> frames={} elapsed_s={:.3f} fps={:.2f} mean={:.3f} stddev={:.3f}", sidecar, a_frames, a_elapsed, a_fps, a_mean, a_stddev);
                                if (auto lg = L.get("RealSensorE2E.Analyzer")) lg->flush();
                            }
                        } else {
                            // Fallback: parse the analyzer log and also dump it into the test logger for visibility
                            const char* summaryLog = "logs/data_analyzer.log";
                            FILE* sf = fopen(summaryLog, "r");
                            if (sf) {
                                char logline[2048];
                                while (fgets(logline, sizeof(logline), sf)) {
                                    size_t Llen = strlen(logline);
                                    if (Llen > 0 && logline[Llen-1] == '\n') logline[Llen-1] = '\0';
                                    L.get("RealSensorE2E.Analyzer")->info("ANALYZER_LOG: {}", logline);
                                }
                                if (auto lg = L.get("RealSensorE2E.Analyzer")) lg->flush();
                                // Rewind and attempt to parse ANALYSIS_SUMMARY
                                rewind(sf);
                                while (fgets(logline, sizeof(logline), sf)) {
                                    char* p = strstr(logline, "ANALYSIS_SUMMARY:");
                                    if (!p) continue;
                                    char* brace = strchr(p, '{');
                                    if (!brace) continue;
                                    if (sscanf(brace, "{\"frames\":%d,\"elapsed_s\":%lf,\"fps\":%lf,\"mean_depth\":%lf,\"stddev_depth\":%lf}", &a_frames, &a_elapsed, &a_fps, &a_mean, &a_stddev) == 5) {
                                        L.get("RealSensorE2E.Analyzer")->info("Parsed ANALYSIS_SUMMARY: frames={} elapsed_s={:.3f} fps={:.2f} mean_depth={:.3f} stddev_depth={:.3f}", a_frames, a_elapsed, a_fps, a_mean, a_stddev);
                                        if (auto lg = L.get("RealSensorE2E.Analyzer")) lg->flush();
                                        break;
                                    }
                                }
                                fclose(sf);
                            } else {
                                L.get("RealSensorE2E.Analyzer")->warn("Could not open analyzer log '{}' to parse summary", summaryLog);
                            }
                        }

                        // Variant 1: enforce thresholds with sensible defaults, but allow overrides via env vars
                        double fps_min = 15.0;
                        double stddev_max = 0.06;
                        double mean_min = 0.4; // meters
                        double mean_max = 4.5; // meters

                        if (const char* v = std::getenv("CALDERA_ANALYZER_FPS_MIN")) fps_min = atof(v);
                        if (const char* v = std::getenv("CALDERA_ANALYZER_STDDEV_MAX")) stddev_max = atof(v);
                        if (const char* v = std::getenv("CALDERA_ANALYZER_MEAN_DEPTH_MIN")) mean_min = atof(v);
                        if (const char* v = std::getenv("CALDERA_ANALYZER_MEAN_DEPTH_MAX")) mean_max = atof(v);

                        L.get("RealSensorE2E.Analyzer")->info("Applying analyzer thresholds: fps>={:.2f}, stddev<={:.3f}, mean_depth in [{:.3f},{:.3f}]", fps_min, stddev_max, mean_min, mean_max);
                        if (auto lg = L.get("RealSensorE2E.Analyzer")) lg->flush();

                        // Enforce checks (use gtest EXPECT so we record failures but continue tearing down)
                        EXPECT_GE(a_fps, fps_min) << "Analyzer fps below threshold";
                        EXPECT_LE(a_stddev, stddev_max) << "Analyzer stddev_depth above threshold";
                        EXPECT_GE(a_mean, mean_min) << "Analyzer mean_depth below threshold";
                        EXPECT_LE(a_mean, mean_max) << "Analyzer mean_depth above threshold";
                    }
                }
                // cleanup recording file
                ::unlink(tmpFile.c_str());
            }
        }
    }
}
