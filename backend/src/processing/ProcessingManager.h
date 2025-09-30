#ifndef CALDERA_BACKEND_PROCESSING_MANAGER_H
#define CALDERA_BACKEND_PROCESSING_MANAGER_H

#include <functional>
#include <memory>
#include <vector>

#include "common/DataTypes.h"

namespace spdlog { class logger; }

namespace caldera::backend::processing {

class ProcessingManager {
public:
    using WorldFrameCallback = std::function<void(const data::WorldFrame&)>;

    ProcessingManager(std::shared_ptr<spdlog::logger> orchestratorLogger,
                      std::shared_ptr<spdlog::logger> fusionLogger = nullptr);

    void setWorldFrameCallback(WorldFrameCallback cb);

    void processRawData(const data::RawDataPacket& raw);

private:
    std::shared_ptr<spdlog::logger> orch_logger_;
    std::shared_ptr<spdlog::logger> fusion_logger_;
    WorldFrameCallback callback_;
    uint64_t frameCounter_ = 0;
};

} // namespace caldera::backend::processing

#endif // CALDERA_BACKEND_PROCESSING_MANAGER_H
