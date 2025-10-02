// Clean header-only declarations. Implementation is in TestLocalTransportClient.cpp.
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace spdlog { class logger; }

class TestLocalTransportClient {
public:
    struct Config { std::string pipe_s2c; std::string pipe_c2s; };
    explicit TestLocalTransportClient(std::shared_ptr<spdlog::logger> log);
    ~TestLocalTransportClient();

    // Perform HELLO -> receive handshake JSON. Returns true on success.
    bool handshake(const Config& cfg, int timeout_ms = 2000, const std::string& hello = "HELLO_CALDERA_CLIENT_V1\n");
    bool sendHeartbeat();
    bool sendTelemetry(const std::string& jsonLine);
    std::vector<std::string> collectServerStats(int maxLines, int timeout_ms);
    const std::string& handshakeJson() const { return handshake_json_; }
    void closeAll();

private:
    void logError(const char* what, int err);
    std::shared_ptr<spdlog::logger> log_;
    Config cfg_{};
    int wfd_{-1};
    int rfd_{-1};
    std::string handshake_json_;
};
