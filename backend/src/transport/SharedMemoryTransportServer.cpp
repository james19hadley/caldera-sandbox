#include "SharedMemoryTransportServer.h"
#include <spdlog/logger.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include "common/Logger.h"

namespace caldera::backend::transport {

SharedMemoryTransportServer::SharedMemoryTransportServer(std::shared_ptr<spdlog::logger> logger, Config cfg)
    : logger_(std::move(logger)), cfg_(std::move(cfg)) {}

SharedMemoryTransportServer::~SharedMemoryTransportServer() { stop(); }

void SharedMemoryTransportServer::start() {
    if (running_) return;
    if (!ensureMapped()) {
        logger_->error("SharedMemoryTransportServer failed to map shared memory");
        return;
    }
    running_ = true;
    logger_->info("SharedMemoryTransportServer started name={} capacity={}x{}", cfg_.shm_name, cfg_.max_width, cfg_.max_height);
}

void SharedMemoryTransportServer::stop() {
    if (!running_) return;
    running_ = false;
    if (mapped_) {
        munmap(mapped_, mapped_size_);
        mapped_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    logger_->info("SharedMemoryTransportServer stopped");
}

bool SharedMemoryTransportServer::ensureMapped() {
    if (mapped_) return true;
    single_buffer_bytes_ = static_cast<size_t>(cfg_.max_width) * cfg_.max_height * sizeof(float);
    mapped_size_ = sizeof(ShmHeader) + single_buffer_bytes_ * 2;

    fd_ = shm_open(cfg_.shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
        logger_->error("shm_open failed: {}", strerror(errno));
        return false;
    }
    if (ftruncate(fd_, mapped_size_) != 0) {
        logger_->error("ftruncate failed: {}", strerror(errno));
        return false;
    }
    mapped_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (mapped_ == MAP_FAILED) {
        logger_->error("mmap failed: {}", strerror(errno));
        mapped_ = nullptr;
        return false;
    }
    // Initialize header (idempotent if pre-existing but we reset ready flags)
    auto* hdr = reinterpret_cast<ShmHeader*>(mapped_);
    hdr->magic = 0x43414C44;
    hdr->version = 2;
    hdr->active_index = 0;
    hdr->buffers[0] = BufferMeta{}; hdr->buffers[1] = BufferMeta{};
    return true;
}

void SharedMemoryTransportServer::sendWorldFrame(const caldera::backend::common::WorldFrame& frame) {
    if (!running_) return;
    if (!mapped_ && !ensureMapped()) return;
    const auto& hm = frame.heightMap;
    if (hm.width > static_cast<int>(cfg_.max_width) || hm.height > static_cast<int>(cfg_.max_height)) {
        // Rate limited via central Logger singleton (once per 2s)
        caldera::backend::common::Logger::instance().warnRateLimited(logger_->name(), "shm_drop", std::chrono::milliseconds(2000),
            fmt::format("Frame dimensions exceed shm capacity {}x{} vs {}x{} -> dropping", hm.width, hm.height, cfg_.max_width, cfg_.max_height));
        return;
    }
    auto* hdr = reinterpret_cast<ShmHeader*>(mapped_);
    uint32_t write_index = 1 - hdr->active_index; // flip buffer
    BufferMeta &meta = hdr->buffers[write_index];
    meta.ready = 0; // mark invalid while writing
    meta.frame_id = frame.frame_id;
    meta.timestamp_ns = frame.timestamp_ns;
    meta.width = static_cast<uint32_t>(hm.width);
    meta.height = static_cast<uint32_t>(hm.height);
    meta.float_count = static_cast<uint32_t>(hm.data.size());
    // compute buffer base
    char* base = reinterpret_cast<char*>(mapped_) + sizeof(ShmHeader) + write_index * single_buffer_bytes_;
    std::memcpy(base, hm.data.data(), hm.data.size() * sizeof(float));
    __sync_synchronize();
    meta.ready = 1; // publish data
    __sync_synchronize();
    hdr->active_index = write_index; // atomicity coarse; barrier ensures prior writes visible
    if (logger_->should_log(spdlog::level::debug)) {
        logger_->debug("SHM wrote frame id={} idx={} size={}x{} floats={} active={}", meta.frame_id, write_index, meta.width, meta.height, meta.float_count, hdr->active_index);
    }
}

} // namespace caldera::backend::transport
