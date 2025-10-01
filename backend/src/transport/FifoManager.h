#ifndef CALDERA_BACKEND_TRANSPORT_FIFO_MANAGER_H
#define CALDERA_BACKEND_TRANSPORT_FIFO_MANAGER_H

#include <string>
#include <memory>
#include <spdlog/logger.h>

namespace caldera::backend::transport {

class FifoManager {
public:
    explicit FifoManager(std::shared_ptr<spdlog::logger> logger);
    ~FifoManager();

    bool create(const std::string& path, bool recreate = true); // mkfifo; if exists and recreate true -> unlink first
    void remove(); // unlink

    int openForReading(bool blocking = true); // returns fd or -1
    int openForWriting(bool blocking = true); // returns fd or -1
    void closePipe(int fd);

    // Read until \n or EOF; returns empty string on error (and logs)
    std::string readLine(int fd, size_t maxLen = 4096);
    bool writeLine(int fd, const std::string& line); // appends newline if not present

    const std::string& path() const { return path_; }

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::string path_;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_FIFO_MANAGER_H
