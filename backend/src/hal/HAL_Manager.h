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
    using RawDepthFrame = caldera::backend::common::RawDepthFrame;
    using RawDepthFrameCallback = std::function<void(const RawDepthFrame&)>;

    explicit HAL_Manager(std::shared_ptr<spdlog::logger> mainLogger,
             std::shared_ptr<spdlog::logger> udpLogger = nullptr);
    ~HAL_Manager();

    void setDepthFrameCallback(RawDepthFrameCallback cb);

    void start();
    void stop();

private:
    void workerLoop();

    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::logger> udp_logger_;
    RawDepthFrameCallback on_depth_frame_;

    std::thread worker_thread_;
    std::atomic<bool> is_running_{false};
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_MANAGER_H
