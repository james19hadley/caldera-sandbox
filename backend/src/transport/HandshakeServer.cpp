// HandshakeServer.cpp
#include "HandshakeServer.h"
#include "FifoManager.h"

#include <spdlog/logger.h>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <cerrno>
#include <sstream>

using namespace std::chrono_literals;

namespace caldera::backend::transport {

FifoHandshakeServer::FifoHandshakeServer(std::shared_ptr<spdlog::logger> log,
                                         std::shared_ptr<spdlog::logger> trace,
                                         HandshakeConfig cfg)
    : log_(std::move(log)), trace_(std::move(trace)), cfg_(std::move(cfg)) {}

FifoHandshakeServer::~FifoHandshakeServer() { stop(); }

void FifoHandshakeServer::setStaticFields(std::vector<HandshakePayloadField> fields) {
    static_fields_ = std::move(fields);
}

void FifoHandshakeServer::setDynamicJsonBuilder(std::function<std::string()> builder) {
    dynamic_builder_ = std::move(builder);
}

std::string FifoHandshakeServer::buildJson() const {
    if (dynamic_builder_) return dynamic_builder_();
    std::ostringstream oss;
    oss << "{\n";
    for (size_t i=0;i<static_fields_.size();++i) {
        oss << "  \"" << static_fields_[i].key << "\": " << static_fields_[i].value;
        if (i+1 < static_fields_.size()) oss << ",";
        oss << "\n";
    }
    oss << "}";
    return oss.str();
}

void FifoHandshakeServer::start() {
    if (running_.exchange(true)) return;
    if (trace_) trace_->info("FifoHandshakeServer starting on {}", cfg_.pipe_path);
    FifoManager fifo(trace_);
    if (!fifo.create(cfg_.pipe_path, true)) {
        running_.store(false); return; }

    for (int session=0; running_ && session < cfg_.max_sessions; ++session) {
        if (trace_) trace_->info("[hs:{}] Waiting for client (timeout {} ms)...", session, cfg_.timeout_ms);
        int rfd = fifo.openForReading(false);
        if (rfd < 0) { running_.store(false); break; }
        int waited=0; char ch; std::string hello;
        while (waited < cfg_.timeout_ms) {
            ssize_t n = ::read(rfd, &ch, 1);
            if (n == 0) { std::this_thread::sleep_for(10ms); waited+=10; continue; }
            if (n < 0) { if (errno==EAGAIN || errno==EWOULDBLOCK) { std::this_thread::sleep_for(10ms); waited+=10; continue; } break; }
            if (ch == '\n') break; else hello.push_back(ch);
            if (hello.size() > 256) break; // guard
        }
        fifo.closePipe(rfd);
        if (hello.empty()) { if(trace_) trace_->warn("[hs:{}] timeout waiting HELLO", session); continue; }
        if (trace_) trace_->info("[hs:{}] got='{}'", session, hello);
        if (hello != client_hello_) { if(trace_) trace_->error("[hs:{}] invalid HELLO", session); continue; }
        int wfd = fifo.openForWriting(true);
        if (wfd < 0) { if(trace_) trace_->error("[hs:{}] open write failed", session); continue; }
        std::string json = buildJson();
        fifo.writeLine(wfd, json); // ensures newline
        fifo.closePipe(wfd);
        if (trace_) trace_->info("[hs:{}] handshake complete", session);
    }
    fifo.remove();
    running_.store(false);
    if (trace_) trace_->info("FifoHandshakeServer stopped");
}

void FifoHandshakeServer::stop() {
    running_.store(false);
}

} // namespace caldera::backend::transport
