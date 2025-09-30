#include "ProcessingManager.h"

#include <spdlog/logger.h>

namespace caldera::backend::processing {

ProcessingManager::ProcessingManager(std::shared_ptr<spdlog::logger> orchestratorLogger,
                                     std::shared_ptr<spdlog::logger> fusionLogger)
    : orch_logger_(std::move(orchestratorLogger)), fusion_logger_(std::move(fusionLogger)) {}

void ProcessingManager::setWorldFrameCallback(WorldFrameCallback cb) { callback_ = std::move(cb); }

void ProcessingManager::processRawData(const data::RawDataPacket& raw) {
    if ((frameCounter_ % 60) == 0) {
        orch_logger_->info("Processing raw packet source={} bytes={} frameCounter={}", raw.sourceId, raw.payload.size(), frameCounter_);
    }
    // Very trivial fake fusion step
    data::WorldFrame frame;
    frame.frameIndex = frameCounter_;
    frame.timestamp_ns = raw.timestamp_ns;
    frame.debugInfo = "synthetic world frame";

    if (fusion_logger_ && fusion_logger_->should_log(spdlog::level::trace)) {
        fusion_logger_->trace("Fused frame {} (raw payload {})", frame.frameIndex, raw.payload.size());
    }

    ++frameCounter_;
    if (callback_) callback_(frame);
}

} // namespace caldera::backend::processing
