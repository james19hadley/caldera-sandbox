#include "ProcessingManager.h"

#include <spdlog/logger.h>

namespace caldera::backend::processing {

ProcessingManager::ProcessingManager(std::shared_ptr<spdlog::logger> orchestratorLogger,
                                     std::shared_ptr<spdlog::logger> fusionLogger,
                                     float depthToHeightScale)
    : orch_logger_(std::move(orchestratorLogger)), fusion_logger_(std::move(fusionLogger)) {
    if (depthToHeightScale > 0.0f) {
        scale_ = depthToHeightScale;
    } else {
        if (const char* env = std::getenv("CALDERA_DEPTH_SCALE")) {
            try {
                float v = std::stof(env);
                if (v > 0.0f && v < 1.0f) scale_ = v; // basic sanity
            } catch(...) {}
        }
    }
}

void ProcessingManager::setWorldFrameCallback(WorldFrameCallback cb) { callback_ = std::move(cb); }

void ProcessingManager::processRawDepthFrame(const RawDepthFrame& raw) {
    if ((frameCounter_ % 60) == 0) {
        orch_logger_->info("Processing depth frame sensor={} w={} h={} frameCounter={}", raw.sensorId, raw.width, raw.height, frameCounter_);
    }
    WorldFrame frame;
    frame.timestamp_ns = raw.timestamp_ns;
    frame.frame_id = frameCounter_;
    frame.heightMap.width = raw.width;
    frame.heightMap.height = raw.height;
    frame.heightMap.data.resize(static_cast<size_t>(raw.width) * raw.height);
    // Simple conversion: scale uint16 depth (0..1500) to float meters (0..1.5) approximation
    const float scale = scale_;
    float minV = std::numeric_limits<float>::infinity();
    float maxV = -std::numeric_limits<float>::infinity();
    double sum = 0.0;
    const size_t N = std::min(frame.heightMap.data.size(), raw.data.size());
    for (size_t i = 0; i < N; ++i) {
        float v = static_cast<float>(raw.data[i]) * scale;
        frame.heightMap.data[i] = v;
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
        sum += v;
    }
    // Apply optional filter (currently likely NoOp)
    if (height_filter_) {
        height_filter_->apply(frame.heightMap.data, frame.heightMap.width, frame.heightMap.height);
    }
    if (fusion_logger_ && fusion_logger_->should_log(spdlog::level::trace)) {
        fusion_logger_->trace("Frame {} depth->height converted N={} min={:.3f} max={:.3f} avg={:.3f}",
            frameCounter_, N, minV, maxV, N ? static_cast<float>(sum / N) : 0.0f);
    }
    // Every 120 frames log a summarized info line (roughly every 4s at 30 FPS)
    if (frameCounter_ % 120 == 0 && orch_logger_->should_log(spdlog::level::info)) {
        orch_logger_->info("WorldFrame#{} stats min={:.3f} max={:.3f} avg={:.3f}",
            frame.frame_id, minV, maxV, N ? static_cast<float>(sum / N) : 0.0f);
    }
    if (fusion_logger_ && fusion_logger_->should_log(spdlog::level::trace)) {
    fusion_logger_->trace("Built WorldFrame {} depth->height conversion done", frame.frame_id);
    }
    ++frameCounter_;
    if (callback_) callback_(frame);
}

} // namespace caldera::backend::processing
