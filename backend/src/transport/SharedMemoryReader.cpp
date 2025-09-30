#include "SharedMemoryReader.h"
#include <spdlog/logger.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace caldera::backend::transport {

SharedMemoryReader::~SharedMemoryReader() { close(); }

bool SharedMemoryReader::open(const std::string& shm_name, uint32_t max_width, uint32_t max_height) {
    if (mapped_) return true;
    single_buffer_bytes_ = static_cast<size_t>(max_width) * max_height * sizeof(float);
    mapped_size_ = sizeof(ShmHeader) + single_buffer_bytes_ * 2;
    fd_ = shm_open(shm_name.c_str(), O_RDONLY, 0666);
    if (fd_ < 0) return false;
    mapped_ = mmap(nullptr, mapped_size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (mapped_ == MAP_FAILED) { mapped_ = nullptr; return false; }
    auto* hdr = reinterpret_cast<ShmHeader*>(mapped_);
    if (hdr->magic != 0x43414C44 || hdr->version != 2) {
        return false;
    }
    return true;
}

void SharedMemoryReader::close() {
    if (mapped_) { munmap(mapped_, mapped_size_); mapped_ = nullptr; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

std::optional<SharedMemoryReader::FrameView> SharedMemoryReader::latest() {
    if (!mapped_) return std::nullopt;
    auto* hdr = reinterpret_cast<ShmHeader*>(mapped_);
    uint32_t idx = hdr->active_index;
    if (idx > 1) return std::nullopt;
    BufferMeta meta = hdr->buffers[idx];
    if (meta.ready != 1) return std::nullopt;
    char* base = reinterpret_cast<char*>(mapped_) + sizeof(ShmHeader) + idx * single_buffer_bytes_;
    FrameView fv{ meta.frame_id, meta.timestamp_ns, meta.width, meta.height, reinterpret_cast<const float*>(base), meta.float_count };
    return fv;
}

} // namespace caldera::backend::transport
