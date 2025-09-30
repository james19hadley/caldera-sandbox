#ifndef CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H

#include <memory>
#include <atomic>
#include <thread>

#include "ITransportServer.h"

namespace caldera::backend::transport {

class LocalTransportServer : public ITransportServer {
public:
    explicit LocalTransportServer(std::shared_ptr<spdlog::logger> logger,
                                  std::shared_ptr<spdlog::logger> handshakeLogger = nullptr);
    ~LocalTransportServer() override;

    void start() override;
    void stop() override;
    void sendWorldFrame(const data::WorldFrame& frame) override;

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::logger> handshake_logger_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_LOCAL_TRANSPORT_SERVER_H
