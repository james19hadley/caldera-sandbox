#ifndef CALDERA_BACKEND_HAL_MANAGER_H
#define CALDERA_BACKEND_HAL_MANAGER_H

#include <functional>
#include <memory>
#include <thread>
#include <atomic>

#include "common/DataTypes.h"

namespace spdlog { class logger; }

namespace caldera::backend::hal {

class HAL_Manager {
public:
    using RawDataCallback = std::function<void(const data::RawDataPacket&)>;

    explicit HAL_Manager(std::shared_ptr<spdlog::logger> mainLogger,
                         std::shared_ptr<spdlog::logger> udpLogger = nullptr);
    ~HAL_Manager();

    void setRawDataCallback(RawDataCallback cb);

    void start();
    void stop();

private:
    void runMockLoop();

    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::logger> udp_logger_;
    RawDataCallback callback_;

    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_MANAGER_H
