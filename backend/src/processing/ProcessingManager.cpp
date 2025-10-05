#include "ProcessingManager.h"

#include <spdlog/logger.h>
#include <limits>
#include <cmath>

using caldera::backend::common::Point3D;
using caldera::backend::processing::InternalPointCloud;
using caldera::backend::processing::TransformParameters;

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
    InternalPointCloud cloudIn;
    InternalPointCloud cloudFiltered;
    lastValidationSummary_ = {};
    buildAndValidatePointCloud(raw, cloudIn, lastValidationSummary_);

    // Prepare temporary height buffer for filter (z values, invalid -> NaN)
    std::vector<float> heightMap;
    heightMap.reserve(cloudIn.points.size());
    for (const auto& p : cloudIn.points) {
        if (p.valid && std::isfinite(p.z)) heightMap.push_back(p.z);
        else heightMap.push_back(std::numeric_limits<float>::quiet_NaN());
    }

    // Apply temporal / other height filters (operate in-place on heightMap)
    if (height_filter_) {
        height_filter_->apply(heightMap, cloudIn.width, cloudIn.height);
    }

    // Rebuild filtered cloud (reuse cloudIn layout)
    cloudFiltered = cloudIn;
    for (size_t i = 0; i < heightMap.size(); ++i) {
        float v = heightMap[i];
        cloudFiltered.points[i].z = v;
        cloudFiltered.points[i].valid = std::isfinite(v);
    }

    // Assemble WorldFrame
    WorldFrame frame;
    frame.timestamp_ns = raw.timestamp_ns;
    frame.frame_id = frameCounter_;
    frame.heightMap.width = cloudFiltered.width;
    frame.heightMap.height = cloudFiltered.height;
    frame.heightMap.data.resize(cloudFiltered.points.size());

    float minV = std::numeric_limits<float>::infinity();
    float maxV = -std::numeric_limits<float>::infinity();
    double sum = 0.0;
    for (size_t i = 0; i < cloudFiltered.points.size(); ++i) {
        float z = cloudFiltered.points[i].z;
        if (!std::isfinite(z)) {
            // Replace invalid with 0.0f (stable baseline) – alternative: keep last valid history (already done by filter)
            z = 0.0f;
        }
        frame.heightMap.data[i] = z;
        minV = std::min(minV, z);
        maxV = std::max(maxV, z);
        sum += z;
    }

    static int traceEvery = [](){ if(const char* env = std::getenv("CALDERA_LOG_FRAME_TRACE_EVERY")) { int v = std::atoi(env); return v>0?v:0; } return 0; }();
    if (traceEvery>0 && fusion_logger_ && fusion_logger_->should_log(spdlog::level::trace) && (frameCounter_ % traceEvery)==0) {
        fusion_logger_->trace("Frame {} validated valid={} invalid={} min={:.3f} max={:.3f} avg={:.3f}",
            frameCounter_, lastValidationSummary_.valid, lastValidationSummary_.invalid,
            minV, maxV,
            frame.heightMap.data.empty()?0.0f:static_cast<float>(sum / frame.heightMap.data.size()));
    }
    if (frameCounter_ % 120 == 0 && orch_logger_->should_log(spdlog::level::info)) {
        orch_logger_->info("WorldFrame#{} stats valid={} invalid={} min={:.3f} max={:.3f} avg={:.3f}",
            frame.frame_id, lastValidationSummary_.valid, lastValidationSummary_.invalid,
            minV, maxV, frame.heightMap.data.empty()?0.0f:static_cast<float>(sum / frame.heightMap.data.size()));
    }

    ++frameCounter_;
    if (callback_) callback_(frame);
}

} // namespace caldera::backend::processing

namespace caldera::backend::processing {

void ProcessingManager::buildAndValidatePointCloud(const RawDepthFrame& raw,
                                                   InternalPointCloud& cloud,
                                                   FrameValidationSummary& summary) {
    cloud.resize(raw.width, raw.height);
    cloud.timestamp_ns = raw.timestamp_ns;
    const float depthScale = scale_;
    // Apply env-based elevation offsets to min/max planes once (if transform params provided)
    if (transformParamsReady_ && !planeOffsetsApplied_) {
        const char* envMin = std::getenv("CALDERA_ELEV_MIN_OFFSET_M");
        const char* envMax = std::getenv("CALDERA_ELEV_MAX_OFFSET_M");
        if (envMin || envMax) {
            // Interpret minValidPlane/maxValidPlane currently as base-derived; adjust d term by offsets.
            auto adjustPlane = [](std::array<float,4>& plane, float delta){ plane[3] += delta * plane[2]; };
            if (envMin) {
                try { float v = std::stof(envMin); adjustPlane(transformParams_.minValidPlane, -v); } catch(...) {}
            }
            if (envMax) {
                try { float v = std::stof(envMax); adjustPlane(transformParams_.maxValidPlane, -v); } catch(...) {}
            }
        }
        planeOffsetsApplied_ = true;
    }

    // Simple spatial scaling for x,y — placeholder until intrinsics wired.
    const float pixelScaleX = 1.0f;
    const float pixelScaleY = 1.0f;
    const float cx = (raw.width  - 1) * 0.5f;
    const float cy = (raw.height - 1) * 0.5f;

    const size_t N = std::min<size_t>(raw.data.size(), static_cast<size_t>(raw.width * raw.height));
    for (size_t idx = 0; idx < N; ++idx) {
        int y = static_cast<int>(idx / raw.width);
        int x = static_cast<int>(idx % raw.width);

        Point3D& pt = cloud.points[idx];
        uint16_t d = raw.data[idx];

        // raw invalid depth (zero means no return for many sensors)
        if (d == 0) {
            pt = Point3D(0,0,std::numeric_limits<float>::quiet_NaN(), false);
            ++summary.invalid;
            continue;
        }

        float z = static_cast<float>(d) * depthScale; // meters (approx)
        float wx = (static_cast<float>(x) - cx) * pixelScaleX;
        float wy = (static_cast<float>(y) - cy) * pixelScaleY;

        bool valid = std::isfinite(z);
        if (valid && transformParamsReady_) {
            // World-space plane validation (approx) using z as elevation and ignoring rotation (placeholder until full transform)
            // Evaluate point as (x,y,z) against provided planes.
            float minValue = transformParams_.minValidPlane[0]*wx + transformParams_.minValidPlane[1]*wy + transformParams_.minValidPlane[2]*z + transformParams_.minValidPlane[3];
            float maxValue = transformParams_.maxValidPlane[0]*wx + transformParams_.maxValidPlane[1]*wy + transformParams_.maxValidPlane[2]*z + transformParams_.maxValidPlane[3];
            if (!(minValue >= 0.0f && maxValue <= 0.0f)) {
                valid = false;
            }
        }
        if (!valid) {
            pt = Point3D(wx, wy, std::numeric_limits<float>::quiet_NaN(), false);
            ++summary.invalid;
            continue;
        }
        pt = Point3D(wx, wy, z, true);
        ++summary.valid;
    }
}

} // namespace caldera::backend::processing
