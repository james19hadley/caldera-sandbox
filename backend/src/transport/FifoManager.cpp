#include "FifoManager.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace caldera::backend::transport {

FifoManager::FifoManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

FifoManager::~FifoManager() {
    // Do not automatically unlink on destruction; explicit remove() preferred
}

bool FifoManager::create(const std::string& path, bool recreate) {
    path_ = path;
    if (recreate) {
        ::unlink(path_.c_str()); // ignore result
    }
    if (::mkfifo(path_.c_str(), 0660) < 0) {
        if (errno == EEXIST && !recreate) {
            return true; // already exists and allowed
        }
        if (logger_) logger_->error("mkfifo failed for {}: {}", path_, std::strerror(errno));
        return false;
    }
    if (logger_) logger_->info("FIFO created at {}", path_);
    return true;
}

void FifoManager::remove() {
    if (path_.empty()) return;
    if (::unlink(path_.c_str()) == 0) {
        if (logger_) logger_->info("FIFO removed: {}", path_);
    }
}

int FifoManager::openForReading(bool blocking) {
    int flags = O_RDONLY;
    if (!blocking) flags |= O_NONBLOCK;
    int fd = ::open(path_.c_str(), flags);
    if (fd < 0 && logger_) logger_->error("open (read) failed {}: {}", path_, std::strerror(errno));
    return fd;
}

int FifoManager::openForWriting(bool blocking) {
    int flags = O_WRONLY;
    if (!blocking) flags |= O_NONBLOCK;
    int fd = ::open(path_.c_str(), flags);
    if (fd < 0 && logger_) logger_->error("open (write) failed {}: {}", path_, std::strerror(errno));
    return fd;
}

void FifoManager::closePipe(int fd) {
    if (fd >= 0) ::close(fd);
}

std::string FifoManager::readLine(int fd, size_t maxLen) {
    std::string out;
    out.reserve(128);
    char ch;
    while (out.size() < maxLen) {
        ssize_t n = ::read(fd, &ch, 1);
        if (n == 0) { // EOF
            break;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            if (logger_) logger_->error("read error {}: {}", path_, std::strerror(errno));
            return {};
        }
        if (ch == '\n') break;
        out.push_back(ch);
    }
    return out;
}

bool FifoManager::writeLine(int fd, const std::string& line) {
    std::string payload = line;
    if (payload.empty() || payload.back() != '\n') payload.push_back('\n');
    size_t total = 0;
    while (total < payload.size()) {
        ssize_t n = ::write(fd, payload.data() + total, payload.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (logger_) logger_->error("write error {}: {}", path_, std::strerror(errno));
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

} // namespace caldera::backend::transport
