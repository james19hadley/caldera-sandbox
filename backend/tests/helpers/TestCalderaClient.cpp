#include "TestCalderaClient.h"
#include "common/Logger.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <thread>
#include <algorithm>
#include <cmath>

using namespace std::chrono_literals;
using caldera::backend::transport::SharedMemoryWorldFrameClient;
using caldera::backend::transport::IWorldFrameClient;

namespace {
static void logErr(std::shared_ptr<spdlog::logger> lg, const char* what, int err){ if(lg) lg->error("Client {} error: {}", what, strerror(err)); }
}

TestCalderaClient::TestCalderaClient(std::shared_ptr<spdlog::logger> log) : log_(std::move(log)) {}
TestCalderaClient::~TestCalderaClient(){ disconnectControl(); disconnectData(); }

bool TestCalderaClient::connectControl(const ControlConfig& cfg) {
    disconnectControl(); ctrl_cfg_ = cfg; handshake_json_.clear();
    wfd_ = ::open(cfg.pipe_c2s.c_str(), O_WRONLY);
    if (wfd_ < 0) { logErr(log_, "open c2s", errno); return false; }
    if (::write(wfd_, cfg.hello.data(), cfg.hello.size()) != (ssize_t)cfg.hello.size()) { logErr(log_, "write HELLO", errno); closeControl(); return false; }
    rfd_ = ::open(cfg.pipe_s2c.c_str(), O_RDONLY | O_NONBLOCK);
    if (rfd_ < 0) { logErr(log_, "open s2c", errno); closeControl(); return false; }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.handshake_timeout_ms);
    char buf[256];
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = ::read(rfd_, buf, sizeof(buf));
        if (n > 0) {
            handshake_json_.append(buf, buf+n);
            if (handshake_json_.find('}') != std::string::npos) break;
        } else if (n == 0) {
            std::this_thread::sleep_for(10ms);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { std::this_thread::sleep_for(10ms); continue; }
            break;
        }
    }
    ctrl_connected_ = handshake_json_.find("protocol_version") != std::string::npos;
    if (!ctrl_connected_) closeControl();
    return ctrl_connected_;
}

void TestCalderaClient::disconnectControl() { closeControl(); }
void TestCalderaClient::closeControl() {
    if (rfd_ >= 0) { ::close(rfd_); rfd_=-1; }
    if (wfd_ >= 0) { ::close(wfd_); wfd_=-1; }
    ctrl_connected_ = false; handshake_json_.clear();
}

bool TestCalderaClient::sendHeartbeat() {
    if (wfd_ < 0) return false; const char* hb = "{\"type\":\"heartbeat\"}\n"; return ::write(wfd_, hb, strlen(hb)) == (ssize_t)strlen(hb);
}
bool TestCalderaClient::sendTelemetry(const std::string& jsonLine) {
    if (wfd_ < 0) return false; std::string line = jsonLine; if (line.empty() || line.back()!='\n') line.push_back('\n'); return ::write(wfd_, line.data(), line.size()) == (ssize_t)line.size();
}
std::vector<std::string> TestCalderaClient::collectServerStats(int maxLines, int timeout_ms) {
    std::vector<std::string> out; if (rfd_ < 0) return out; auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms); std::string bufLine; char ch;
    while ((int)out.size() < maxLines && std::chrono::steady_clock::now() < deadline) {
        ssize_t n = ::read(rfd_, &ch, 1);
        if (n > 0) { if (ch=='\n') { if (!bufLine.empty()) { if (bufLine.find("server_stats")!=std::string::npos) out.push_back(bufLine); bufLine.clear(); } } else bufLine.push_back(ch); }
        else if (n==0) { std::this_thread::sleep_for(20ms); }
        else { if (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR){ std::this_thread::sleep_for(20ms); continue;} break; }
    }
    return out;
}

bool TestCalderaClient::connectData(const ShmDataConfig& cfg) {
    disconnectData(); shm_cfg_ = cfg; verify_checksum_ = cfg.verify_checksum; latencies_ns_.clear(); stats_ = {}; last_seen_frame_id_ = std::numeric_limits<uint64_t>::max();
    auto logger = log_;
    auto implCfg = SharedMemoryWorldFrameClient::Config{cfg.shm_name, cfg.max_width, cfg.max_height};
    data_client_ = std::make_unique<SharedMemoryWorldFrameClient>(logger, implCfg);
    data_connected_ = data_client_->connect(cfg.connect_timeout_ms);
    if (!data_connected_) { data_client_.reset(); }
    else {
        connect_time_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    return data_connected_;
}

void TestCalderaClient::disconnectData() { closeData(); }
void TestCalderaClient::closeData(){ if (data_client_) { data_client_->disconnect(); data_client_.reset(); } data_connected_=false; }

std::optional<TestCalderaClient::FrameView> TestCalderaClient::latest() {
    if (!data_client_) return std::nullopt;
    auto opt = data_client_->latest(verify_checksum_);
    if (!opt) return std::nullopt;
    FrameView fv = static_cast<FrameView&>(*opt);
    // Only update stats on distinct frame ids to avoid double counting when polling same frame repeatedly
    bool isNew = (stats_.distinct_frames == 0) || (fv.frame_id != stats_.last_frame_id);
    uint64_t latency_ns = 0;
    if (enable_latency_ && isNew) {
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (fv.timestamp_ns > 0 && fv.timestamp_ns <= now_ns) {
            latency_ns = now_ns - fv.timestamp_ns;
            // Filter out unrealistic values (e.g., > 5 seconds) which may indicate clock mismatch or stale timestamps
            if (latency_ns > 5ULL * 1000ULL * 1000ULL * 1000ULL) latency_ns = 0;
        }
    }
    if (isNew) updateFrameStats(fv, latency_ns);
    return fv;
}

void TestCalderaClient::updateFrameStats(const FrameView& fv, uint64_t latency_ns) {
    // Base stats come from underlying client stats() only when we snapshot externally; we also enrich here.
    if (stats_.distinct_frames == 0) {
        stats_.last_frame_id = fv.frame_id;
        stats_.distinct_frames = 1;
    } else if (fv.frame_id != stats_.last_frame_id) {
        uint64_t gap = 0; if (fv.frame_id > stats_.last_frame_id) gap = fv.frame_id - stats_.last_frame_id; // ignore wrap/retrograde
        if (gap > 1) { uint64_t skipped = gap - 1; stats_.total_skipped += skipped; if (skipped > stats_.max_gap) stats_.max_gap = skipped; }
        stats_.last_frame_id = fv.frame_id; ++stats_.distinct_frames;
    }
    ++stats_.frames_observed;
    if (fv.checksum_algorithm == 1 && fv.checksum != 0) {
        ++stats_.checksum_present; // if checksum_valid true -> verified
        if (fv.checksum_valid) ++stats_.checksum_verified; else ++stats_.checksum_mismatch;
    }
    if (enable_latency_ && latency_ns > 0) {
        if (latencies_ns_.size() < latency_cap_) latencies_ns_.push_back(latency_ns); else {
            // Simple reservoir: replace random element (optional). For now keep first  latency_cap_ samples.
        }
        stats_.latency_samples = latencies_ns_.size();
    }
}

bool TestCalderaClient::waitForDistinctFrames(uint64_t targetDistinctFrames, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)latest();
        if (stats_.distinct_frames >= targetDistinctFrames) return true;
        std::this_thread::sleep_for(10ms);
    }
    return stats_.distinct_frames >= targetDistinctFrames;
}

bool TestCalderaClient::waitForProgress(uint64_t minAdvance, int timeout_ms) {
    uint64_t startId = stats_.last_frame_id;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)latest();
        if (stats_.last_frame_id >= startId + minAdvance) return true;
        std::this_thread::sleep_for(15ms);
    }
    return stats_.last_frame_id >= startId + minAdvance;
}

bool TestCalderaClient::waitNoAdvance(uint64_t window_ms) {
    uint64_t startId = stats_.last_frame_id; auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(window_ms);
    while (std::chrono::steady_clock::now() < end) {
        (void)latest();
        if (stats_.last_frame_id != startId) return false;
        std::this_thread::sleep_for(20ms);
    }
    return true;
}

TestCalderaClient::Stats TestCalderaClient::stats() const { return stats_; }

TestCalderaClient::LatencyStats TestCalderaClient::latencyStats() const {
    LatencyStats ls; if (latencies_ns_.empty()) return ls; ls.count = latencies_ns_.size();
    // Copy & sort for percentile (cost acceptable for test sizes)
    std::vector<uint64_t> sorted = latencies_ns_; std::sort(sorted.begin(), sorted.end());
    unsigned __int128 sum = 0; for (auto v: sorted) sum += v;
    double mean_ns = (double) (sum / sorted.size());
    size_t p95_idx = (size_t)std::ceil(0.95 * sorted.size()) - 1; if (p95_idx >= sorted.size()) p95_idx = sorted.size()-1;
    ls.mean_ms = mean_ns / 1e6; ls.p95_ms = sorted[p95_idx] / 1e6; ls.max_ms = sorted.back() / 1e6; return ls;
}
