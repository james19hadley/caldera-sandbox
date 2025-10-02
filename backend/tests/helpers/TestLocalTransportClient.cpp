#include "TestLocalTransportClient.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <chrono>
#include <thread>
#include "common/Logger.h"

TestLocalTransportClient::TestLocalTransportClient(std::shared_ptr<spdlog::logger> log)
    : log_(std::move(log)) {}

TestLocalTransportClient::~TestLocalTransportClient() { closeAll(); }

void TestLocalTransportClient::logError(const char* what, int err) {
    if (log_) log_->error("Client {} error: {}", what, strerror(err));
}

bool TestLocalTransportClient::handshake(const Config& cfg, int timeout_ms, const std::string& hello) {
    cfg_ = cfg; handshake_json_.clear();
    closeAll();
    wfd_ = ::open(cfg.pipe_c2s.c_str(), O_WRONLY);
    if (wfd_ < 0) { logError("open c2s", errno); return false; }
    if (::write(wfd_, hello.data(), hello.size()) != (ssize_t)hello.size()) { logError("write HELLO", errno); return false; }
    rfd_ = ::open(cfg.pipe_s2c.c_str(), O_RDONLY | O_NONBLOCK);
    if (rfd_ < 0) { logError("open s2c", errno); return false; }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[256];
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = ::read(rfd_, buf, sizeof(buf));
        if (n > 0) {
            handshake_json_.append(buf, buf+n);
            if (handshake_json_.find('}') != std::string::npos) break;
        } else if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
    }
    return handshake_json_.find("protocol_version") != std::string::npos;
}

bool TestLocalTransportClient::sendHeartbeat() {
    if (wfd_ < 0) return false;
    const char* hb = "{\"type\":\"heartbeat\"}\n";
    return ::write(wfd_, hb, strlen(hb)) == (ssize_t)strlen(hb);
}

bool TestLocalTransportClient::sendTelemetry(const std::string& jsonLine) {
    if (wfd_ < 0) return false;
    std::string line = jsonLine;
    if (line.empty() || line.back() != '\n') line.push_back('\n');
    return ::write(wfd_, line.data(), line.size()) == (ssize_t)line.size();
}

std::vector<std::string> TestLocalTransportClient::collectServerStats(int maxLines, int timeout_ms) {
    std::vector<std::string> out; if (rfd_ < 0) return out;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string bufLine; char ch;
    while ((int)out.size() < maxLines && std::chrono::steady_clock::now() < deadline) {
        ssize_t n = ::read(rfd_, &ch, 1);
        if (n > 0) {
            if (ch == '\n') {
                if (!bufLine.empty()) {
                    if (bufLine.find("server_stats") != std::string::npos) out.push_back(bufLine);
                    bufLine.clear();
                }
            } else bufLine.push_back(ch);
        } else if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            break;
        }
    }
    return out;
}

void TestLocalTransportClient::closeAll() {
    if (rfd_ >= 0) { ::close(rfd_); rfd_ = -1; }
    if (wfd_ >= 0) { ::close(wfd_); wfd_ = -1; }
}
