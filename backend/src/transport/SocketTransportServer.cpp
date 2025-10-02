#include "SocketTransportServer.h"
#include <spdlog/logger.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <atomic>
#include "common/Checksum.h"
#include <thread>

namespace caldera::backend::transport {

SocketTransportServer::SocketTransportServer(std::shared_ptr<spdlog::logger> log, Config cfg)
    : logger_(std::move(log)), cfg_(std::move(cfg)) {}

SocketTransportServer::~SocketTransportServer() { stop(); }

bool SocketTransportServer::parseUnixEndpoint(const std::string& ep, std::string& pathOut) {
    const std::string prefix = "unix:";
    if (ep.rfind(prefix, 0) == 0) { pathOut = ep.substr(prefix.size()); return !pathOut.empty(); }
    return false;
}

bool SocketTransportServer::ensureSocket() {
    if (listen_fd_ >= 0) return true;
    if (!parseUnixEndpoint(cfg_.endpoint, uds_path_)) {
        logger_->error("Only unix: endpoints supported currently (got {})", cfg_.endpoint);
        return false;
    }
    // Remove existing stale socket file
    ::unlink(uds_path_.c_str());
    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { logger_->error("socket() failed: {}", strerror(errno)); return false; }
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    if (uds_path_.size() >= sizeof(addr.sun_path)) { logger_->error("UDS path too long: {}", uds_path_); return false; }
    std::strncpy(addr.sun_path, uds_path_.c_str(), sizeof(addr.sun_path)-1);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        logger_->error("bind() failed: {}", strerror(errno));
        ::close(listen_fd_); listen_fd_ = -1; return false;
    }
    if (::listen(listen_fd_, cfg_.backlog) != 0) {
        logger_->error("listen() failed: {}", strerror(errno));
        ::close(listen_fd_); listen_fd_ = -1; return false;
    }
    // Set non-blocking accept (optional)
    int flags = fcntl(listen_fd_, F_GETFL, 0); fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);
    return true;
}

void SocketTransportServer::acceptLoop() {
    while (running_) {
        if (client_fd_ < 0) {
            sockaddr_un caddr{}; socklen_t clen = sizeof(caddr);
            int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (fd >= 0) {
                // set client socket non-blocking to avoid potential blocking send during shutdown
                int cflags = fcntl(fd, F_GETFL, 0);
                (void)fcntl(fd, F_SETFL, cflags | O_NONBLOCK);
                client_fd_ = fd;
                if (logger_) logger_->info("SocketTransportServer accepted client fd={}", client_fd_);
            } else {
                // sleep briefly to avoid busy spin
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else {
            // client connected; sleep and loop (sendWorldFrame will use client_fd_)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

void SocketTransportServer::start() {
    if (running_) return;
    if (!ensureSocket()) return;
    running_ = true;
    if (logger_) logger_->info("SocketTransportServer start endpoint={} backlog={}", cfg_.endpoint, cfg_.backlog);
    accept_thread_ = std::thread([this]{ acceptLoop(); });
}

void SocketTransportServer::closeClient() {
    if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
}

void SocketTransportServer::stop() {
    if (!running_) return;
    running_ = false;
    // Proactively shutdown sockets to wake any blocking syscalls
    if (client_fd_ >= 0) { ::shutdown(client_fd_, SHUT_RDWR); }
    if (listen_fd_ >= 0) { ::shutdown(listen_fd_, SHUT_RDWR); }
    if (accept_thread_.joinable()) accept_thread_.join();
    closeClient();
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
    if (!uds_path_.empty()) ::unlink(uds_path_.c_str());
    if (logger_) logger_->info("SocketTransportServer stopped");
}

void SocketTransportServer::sendWorldFrame(const caldera::backend::common::WorldFrame& frame) {
    if (!running_ || client_fd_ < 0) return; // no client attached
    // Build header
    WireHeader hdr{};
    hdr.magic[0]='C'; hdr.magic[1]='A'; hdr.magic[2]='L'; hdr.magic[3]='D';
    hdr.version = 1;
    hdr.header_bytes = sizeof(WireHeader);
    hdr.frame_id = frame.frame_id;
    hdr.timestamp_ns = frame.timestamp_ns;
    hdr.width = static_cast<uint32_t>(frame.heightMap.width);
    hdr.height = static_cast<uint32_t>(frame.heightMap.height);
    hdr.float_count = static_cast<uint32_t>(frame.heightMap.data.size());
    uint32_t cs = frame.checksum;
    bool need_auto = (cs == 0);
    if (cfg_.checksum_interval_ms > 0 && need_auto && !frame.heightMap.data.empty()) {
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        uint64_t interval_ns = static_cast<uint64_t>(cfg_.checksum_interval_ms) * 1'000'000ULL;
        if (last_checksum_compute_ns_ == 0 || now_ns - last_checksum_compute_ns_ >= interval_ns) {
            cs = caldera::backend::common::crc32(frame.heightMap.data);
            last_checksum_compute_ns_ = now_ns;
        } else {
            cs = 0;
        }
    } else if (need_auto && cfg_.checksum_interval_ms == 0) {
        cs = 0; // disabled
    }
    hdr.checksum = cs;
    hdr.checksum_algorithm = (cs != 0) ? 1 : 0;
    // Write header then payload (best-effort; if write fails once, drop client)
    auto writeAll = [&](const void* buf, size_t bytes)->bool {
        const char* p = reinterpret_cast<const char*>(buf);
        size_t left = bytes;
        while (left > 0) {
            ssize_t w = ::send(client_fd_, p, left, MSG_NOSIGNAL);
            if (w <= 0) return false;
            left -= static_cast<size_t>(w); p += w;
        }
        return true;
    };
    if (!writeAll(&hdr, sizeof(hdr))) { if (logger_) logger_->warn("SocketTransportServer write header failed -> closing client"); closeClient(); return; }
    if (!frame.heightMap.data.empty()) {
        size_t bytes = frame.heightMap.data.size() * sizeof(float);
        if (!writeAll(frame.heightMap.data.data(), bytes)) { if (logger_) logger_->warn("SocketTransportServer write payload failed -> closing client"); closeClient(); return; }
    }
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        uint64_t fid = hdr.frame_id; uint32_t w=hdr.width,h=hdr.height,fc=hdr.float_count,cs=hdr.checksum,alg=hdr.checksum_algorithm;
        logger_->trace("Socket sent frame id={} size={}x{} floats={} checksum={} alg={}", fid, w, h, fc, cs, alg);
    }
}

} // namespace caldera::backend::transport
