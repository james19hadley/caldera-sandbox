#ifndef CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H

#include <memory>
#include <atomic>
#include <thread>

#include "ITransportServer.h"
#include "FifoManager.h"

namespace caldera::backend::transport {

class LocalTransportServer : public ITransportServer {
public:
    explicit LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                                  std::shared_ptr<spdlog::logger> handshakeLogger = nullptr);
    ~LocalTransportServer() override;

    void start() override;
    void stop() override;
    void sendWorldFrame(const caldera::backend::common::WorldFrame& frame) override;

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::logger> handshake_logger_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    FifoManager fifo_{handshake_logger_};
    std::string shm_name_a_;
    std::string shm_name_b_;
    size_t shm_size_ = 0;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H
