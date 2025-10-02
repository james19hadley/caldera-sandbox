#include "SocketWorldFrameClient.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>
#include "common/Checksum.h"

using namespace std::chrono;

namespace caldera::backend::transport {

SocketWorldFrameClient::SocketWorldFrameClient(std::shared_ptr<spdlog::logger> log, Config cfg)
    : log_(std::move(log)), cfg_(std::move(cfg)) {}

SocketWorldFrameClient::~SocketWorldFrameClient() { disconnect(); }

bool SocketWorldFrameClient::parseUnixEndpoint(const std::string& ep, std::string& pathOut) {
    const std::string prefix = "unix:";
    if (ep.rfind(prefix, 0) == 0) { pathOut = ep.substr(prefix.size()); return !pathOut.empty(); }
    return false;
}

bool SocketWorldFrameClient::ensureConnected(uint32_t timeout_ms) {
    if (fd_ >= 0) return true;
    std::string path; if (!parseUnixEndpoint(cfg_.endpoint, path)) return false;
    auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    while (steady_clock::now() < deadline) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
        sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) { ::close(fd); return false; }
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
        // Set non-blocking so we can enforce connect timeout
        int flags = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc == 0 || errno == EINPROGRESS) {
            // Wait for writability up to 200ms per attempt
            fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
            timeval tv; tv.tv_sec = 0; tv.tv_usec = 200 * 1000;
            int sel = ::select(fd+1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0) {
                int err = 0; socklen_t len = sizeof(err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    // Restore flags to blocking mode for reads
                    fcntl(fd, F_SETFL, flags);
                    fd_ = fd; header_pending_ = true; return true;
                }
            }
        }
        ::close(fd);
        std::this_thread::sleep_for(milliseconds(50));
    }
    return false;
}

bool SocketWorldFrameClient::connect(uint32_t timeout_ms) { return ensureConnected(timeout_ms); }

void SocketWorldFrameClient::disconnect() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    payload_.clear(); last_ = {}; ret_ = {}; stats_ = {}; header_pending_ = true;
}

bool SocketWorldFrameClient::readExact(void* buf, size_t bytes) {
    char* p = reinterpret_cast<char*>(buf); size_t left = bytes;
    while (left > 0) {
        ssize_t r = ::recv(fd_, p, left, 0);
        if (r <= 0) return false;
        left -= static_cast<size_t>(r); p += r;
    }
    return true;
}

bool SocketWorldFrameClient::readHeader(WireHeader& hdr) {
    if (!readExact(&hdr, sizeof(hdr))) return false;
    if (!(hdr.magic[0]=='C' && hdr.magic[1]=='A' && hdr.magic[2]=='L' && hdr.magic[3]=='D')) return false;
    if (hdr.version != 1 || hdr.header_bytes != sizeof(WireHeader)) return false;
    return true;
}

bool SocketWorldFrameClient::readPayload(size_t bytes) {
    payload_.resize(bytes/sizeof(float));
    return readExact(payload_.data(), bytes);
}

std::optional<IWorldFrameClient::FrameView> SocketWorldFrameClient::latest(bool verify_checksum) {
    if (fd_ < 0) return std::nullopt;
    // Blocking read a single frame (header + payload)
    WireHeader hdr{};
    if (!readHeader(hdr)) { disconnect(); return std::nullopt; }
    size_t bytes = static_cast<size_t>(hdr.float_count) * sizeof(float);
    if (bytes > 0) {
        if (!readPayload(bytes)) { disconnect(); return std::nullopt; }
    } else {
        payload_.clear();
    }

    // Prepare FrameView
    ret_.frame_id = hdr.frame_id;
    ret_.timestamp_ns = hdr.timestamp_ns;
    ret_.width = hdr.width;
    ret_.height = hdr.height;
    ret_.float_count = hdr.float_count;
    ret_.data = payload_.data();
    ret_.checksum = hdr.checksum;
    ret_.checksum_algorithm = hdr.checksum_algorithm;
    ret_.checksum_valid = true;

    // Distinct counting
    ++stats_.frames_observed;
    if (stats_.distinct_frames == 0 || ret_.frame_id != stats_.last_frame_id) {
        ++stats_.distinct_frames;
        stats_.last_frame_id = ret_.frame_id;
    }

    if (verify_checksum && hdr.checksum_algorithm == 1 && hdr.checksum != 0 && hdr.float_count > 0) {
        ++stats_.checksum_present;
        uint32_t c = caldera::backend::common::crc32(payload_);
        if (c == hdr.checksum) ++stats_.checksum_verified; else { ++stats_.checksum_mismatch; ret_.checksum_valid = false; }
    }
    return ret_;
}

} // namespace caldera::backend::transport
