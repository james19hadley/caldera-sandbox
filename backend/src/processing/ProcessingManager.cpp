#include "ProcessingManager.h"

#include <spdlog/logger.h>
#include <limits>
#include <cmath>
#include "processing/SpatialFilter.h"
#include "tools/calibration/SensorCalibration.h"
#include <chrono>
#include <memory>
#include <string>

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

    // ENV fallback calibration planes (M3):
    // CALDERA_CALIB_MIN_PLANE="a,b,c,d"  CALDERA_CALIB_MAX_PLANE="a,b,c,d"
    // If provided, override transformParams_ planes unless setTransformParameters was already called later.
    auto parsePlane = [](const char* s, std::array<float,4>& out)->bool {
        if(!s) return false; std::string str(s); size_t pos=0; int i=0; while(i<4) { size_t comma=str.find(',',pos); std::string tok = (comma==std::string::npos)?str.substr(pos):str.substr(pos,comma-pos); try{ out[i]=std::stof(tok);}catch(...){ return false;} if(comma==std::string::npos){ ++i; break;} pos=comma+1; ++i; }
        return i==4; };
    bool applied=false; std::array<float,4> minP{0,0,1,-0.5f}; std::array<float,4> maxP{0,0,1,-2.0f};
    if (parsePlane(std::getenv("CALDERA_CALIB_MIN_PLANE"), minP)) applied=true;
    if (parsePlane(std::getenv("CALDERA_CALIB_MAX_PLANE"), maxP)) applied=true;
    if (applied) {
        transformParams_.minValidPlane = minP;
        transformParams_.maxValidPlane = maxP;
        transformParamsReady_ = true;
        planeOffsetsApplied_ = false; // allow elevation offsets to adjust
        if (orch_logger_) orch_logger_->info("Applied env fallback calibration planes.");
    }

    // Metrics enable (static env parse once)
    metricsEnabled_ = [](){ if(const char* e=std::getenv("CALDERA_PROCESSING_STABILITY_METRICS")) return std::atoi(e)==1; return false;}();
    // Adaptive mode: 0=off,1=force spatial on (existing env),2=adaptive
    adaptiveMode_ = [](){ if(const char* e=std::getenv("CALDERA_ADAPTIVE_MODE")) return std::atoi(e); return 0; }();
    // Thresholds (with defaults) for enabling spatial filter adaptively
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_STABILITY_MIN")) { try { adaptiveStabilityMin_ = std::stof(e);}catch(...){} }
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_VARIANCE_MAX")) { try { adaptiveVarianceMax_ = std::stof(e);}catch(...){} }
    // Phase 2 hysteresis & strong mode envs
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_ON_STREAK")) { try { adaptiveOnStreak_ = std::max(1, std::atoi(e)); } catch(...){} }
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_OFF_STREAK")) { try { adaptiveOffStreak_ = std::max(1, std::atoi(e)); } catch(...){} }
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_STRONG_MULT")) { try { adaptiveStrongVarMult_ = std::max(1.0f, std::stof(e)); } catch(...){} }
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_STRONG_STAB_FRACTION")) {
        try {
            float v = std::stof(e);
            if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
            adaptiveStrongStabFrac_ = v;
        } catch(...){}
    }
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_STRONG_DOUBLE_PASS")) { try { adaptiveStrongDoublePass_ = std::atoi(e)!=0; } catch(...){} }
    // Adaptive temporal scale (optional) >1 means blend with previous filtered map when instability
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_TEMPORAL_SCALE")) { try { float v=std::stof(e); if(v>1.0f && v<10.0f) adaptiveTemporalScale_ = v; } catch(...){} }

    // Calibration profile auto-load (M3): if CALDERA_CALIB_SENSOR_ID is set, attempt to
    // load an existing profile from the default calibration directory (or overridden by CALDERA_CALIB_DIR).
    // On success, overrides any env fallback planes.
    if (const char* sensorEnv = std::getenv("CALDERA_CALIB_SENSOR_ID")) {
        caldera::backend::tools::calibration::SensorCalibration calib;
        if (const char* dirEnv = std::getenv("CALDERA_CALIB_DIR")) {
            calib.setCalibrationDirectory(dirEnv);
        }
        caldera::backend::tools::calibration::SensorCalibrationProfile profile;
        if (calib.loadCalibrationProfile(sensorEnv, profile)) {
            applyCalibrationProfile(profile);
            if (orch_logger_) orch_logger_->info("Loaded calibration profile for sensor '{}' (planes applied)", sensorEnv);
        } else {
            if (orch_logger_) orch_logger_->warn("Calibration profile not found for sensor '{}', continuing with fallback/defaults", sensorEnv);
        }
    }
}

void ProcessingManager::setWorldFrameCallback(WorldFrameCallback cb) { callback_ = std::move(cb); }

void ProcessingManager::processRawDepthFrame(const RawDepthFrame& raw) {
    auto tFrameStart = std::chrono::steady_clock::now();
    if ((frameCounter_ % 60) == 0) {
        orch_logger_->info("Processing depth frame sensor={} w={} h={} frameCounter={}", raw.sensorId, raw.width, raw.height, frameCounter_);
    }
    InternalPointCloud cloudIn;
    InternalPointCloud cloudFiltered;
    lastValidationSummary_ = {};
    auto tBuildStart = std::chrono::steady_clock::now();
    buildAndValidatePointCloud(raw, cloudIn, lastValidationSummary_);
    auto tBuildEnd = std::chrono::steady_clock::now();

    // Prepare temporary height buffer for filter (z values, invalid -> NaN)
    std::vector<float> heightMap;
    heightMap.reserve(cloudIn.points.size());
    for (const auto& p : cloudIn.points) {
        if (p.valid && std::isfinite(p.z)) heightMap.push_back(p.z);
        else heightMap.push_back(std::numeric_limits<float>::quiet_NaN());
    }

    // Apply temporal / other height filters (operate in-place on heightMap)
    auto tFilterStart = std::chrono::steady_clock::now();
    if (height_filter_) {
        height_filter_->apply(heightMap, cloudIn.width, cloudIn.height);
    }

    // Optional spatial filter (M2) controlled by env CALDERA_ENABLE_SPATIAL_FILTER=1
    static int baseSpatial = [](){ if(const char* e=std::getenv("CALDERA_ENABLE_SPATIAL_FILTER")) return std::atoi(e); return 0; }();
    // Reconfigurable spatial filter instance (recreated if env changes kernel alt)
    auto& spatialFilterInstance = []() -> SpatialFilter& {
        static std::unique_ptr<SpatialFilter> inst;
        static std::string lastAlt;
        const char* alt = std::getenv("CALDERA_SPATIAL_KERNEL_ALT");
        std::string cur = alt?alt:"";
        if (!inst || cur != lastAlt) {
            inst = std::make_unique<SpatialFilter>(true);
            lastAlt = cur;
        }
        return *inst;
    }();
    bool applySpatial = (baseSpatial == 1);
    bool strongPass = false;
    // Sampling: capture a subset variance before spatial if metrics enabled
    float preSpatialVar = 0.0f; float postSpatialVar = 0.0f; bool sampled=false;
    static int sampleCount = [](){ if(const char* e=std::getenv("CALDERA_SPATIAL_SAMPLE_COUNT")) { int v=std::atoi(e); return v>0? v:1024; } return 1024; }();
    std::vector<size_t> sampleIdx;
    if (metricsEnabled_ && sampleCount>0 && (int)heightMap.size() > sampleCount) {
        // Deterministic pseudo-random selection using linear congruential stepping
        sampleIdx.reserve(sampleCount);
        size_t step = (heightMap.size() / sampleCount);
        if (step==0) step=1;
        size_t seed = (frameCounter_ * 1664525u + 1013904223u) % heightMap.size();
        size_t idx = seed % step; // start near deterministic offset
        for (int i=0; i<sampleCount && idx<heightMap.size(); ++i, idx+=step) {
            sampleIdx.push_back(idx);
        }
        if (!sampleIdx.empty()) {
            // Compute simple variance (ignoring NaNs) of finite values in sample (neighbor diff variance surrogate)
            double sumS=0.0, sumSq=0.0; int n=0;
            for(size_t si: sampleIdx){ float v=heightMap[si]; if(std::isfinite(v)){ sumS+=v; sumSq+=double(v)*v; ++n; } }
            if (n>1) preSpatialVar = static_cast<float>((sumSq - (sumS*sumS)/n)/(n-1));
            sampled = true;
        }
    }
    if (adaptiveMode_ == 2 && metricsEnabled_) {
        if (frameCounter_ > 0) {
            // Classification based on previous frame metrics
            float stab = lastStabilityMetrics_.stabilityRatio;
            float varP = lastStabilityMetrics_.avgVariance;
            bool unstable = (stab < adaptiveStabilityMin_) || (varP > adaptiveVarianceMax_);
            bool stable = !unstable;
            if (unstable) {
                ++unstableStreak_;
                stableStreak_ = 0;
            } else {
                ++stableStreak_;
                unstableStreak_ = 0;
            }
            if (!adaptiveSpatialActive_ && unstable && unstableStreak_ >= static_cast<uint32_t>(adaptiveOnStreak_)) {
                adaptiveSpatialActive_ = true;
                if (orch_logger_) orch_logger_->info("Adaptive spatial ENABLE active after streak={} (stab={:.3f} var={:.5f})", unstableStreak_, stab, varP);
            } else if (adaptiveSpatialActive_ && stable && stableStreak_ >= static_cast<uint32_t>(adaptiveOffStreak_)) {
                adaptiveSpatialActive_ = false;
                if (orch_logger_) orch_logger_->info("Adaptive spatial DISABLE after stable streak={} (stab={:.3f} var={:.5f})", stableStreak_, stab, varP);
            }
            // Strong pass decision (only if we are/will be applying spatial this frame)
            if (adaptiveSpatialActive_) {
                if ((varP > adaptiveVarianceMax_ * adaptiveStrongVarMult_) || (stab < adaptiveStabilityMin_ * adaptiveStrongStabFrac_)) {
                    strongPass = true;
                }
            }
        }
        if (adaptiveSpatialActive_) applySpatial = true;
    }
    if (applySpatial) {
        spatialFilterInstance.apply(heightMap, cloudIn.width, cloudIn.height);
        if (strongPass && adaptiveStrongDoublePass_) {
            spatialFilterInstance.apply(heightMap, cloudIn.width, cloudIn.height);
        }
        if (sampled) {
            double sumS=0.0, sumSq=0.0; int n=0;
            for(size_t si: sampleIdx){ float v=heightMap[si]; if(std::isfinite(v)){ sumS+=v; sumSq+=double(v)*v; ++n; } }
            if (n>1) postSpatialVar = static_cast<float>((sumSq - (sumS*sumS)/n)/(n-1));
        }
    }
    auto tFilterEnd = std::chrono::steady_clock::now();

    // Rebuild filtered cloud (reuse cloudIn layout)
    cloudFiltered = cloudIn;
    for (size_t i = 0; i < heightMap.size(); ++i) {
        float v = heightMap[i];
        cloudFiltered.points[i].z = v;
        cloudFiltered.points[i].valid = std::isfinite(v);
    }

    // Adaptive temporal blending (simple): if unstable (same condition used earlier) and we have previous, blend current with previous filtered heights
    bool adaptiveTemporalApplied = false;
    if (adaptiveTemporalScale_ > 1.0f && frameCounter_>0 && metricsEnabled_) {
        float stab = lastStabilityMetrics_.stabilityRatio;
        float varP = lastStabilityMetrics_.avgVariance;
        bool unstable = (stab < adaptiveStabilityMin_) || (varP > adaptiveVarianceMax_);
        if (unstable && prevFilteredValid_ && prevFilteredHeight_.size()==heightMap.size()) {
            // Blend factor derived from scale: alpha = 1/scale
            float alpha = 1.0f / adaptiveTemporalScale_;
            for(size_t i=0;i<heightMap.size();++i){
                float prev = prevFilteredHeight_[i];
                float cur = heightMap[i];
                if(std::isfinite(prev) && std::isfinite(cur)) {
                    heightMap[i] = alpha*cur + (1.0f-alpha)*prev;
                }
            }
            // Write blended back to cloudFiltered
            for(size_t i=0;i<heightMap.size();++i){
                cloudFiltered.points[i].z = heightMap[i];
                cloudFiltered.points[i].valid = std::isfinite(heightMap[i]);
            }
            adaptiveTemporalApplied = true;
        }
    }

    // Fusion (Phase 0: single-sensor passthrough)
    auto tFuseStart = std::chrono::steady_clock::now();
    fusion_.beginFrame(frameCounter_, cloudFiltered.width, cloudFiltered.height);
    // Build a contiguous temp height array with invalid replaced by 0.0f
    std::vector<float> tempHeights;
    tempHeights.resize(cloudFiltered.points.size());
    for (size_t i=0;i<cloudFiltered.points.size();++i) {
        float z = cloudFiltered.points[i].z;
        if (!std::isfinite(z)) z = 0.0f;
        tempHeights[i] = z;
    }
    FusionInputLayer layer{ raw.sensorId, tempHeights.data(), nullptr, cloudFiltered.width, cloudFiltered.height };
    fusion_.addLayer(layer);
    std::vector<float> fusedHeights; // output buffer
    fusion_.fuse(fusedHeights, nullptr);
    auto tFuseEnd = std::chrono::steady_clock::now();

    // Assemble WorldFrame from fused result
    WorldFrame frame;
    frame.timestamp_ns = raw.timestamp_ns;
    frame.frame_id = frameCounter_;
    frame.heightMap.width = cloudFiltered.width;
    frame.heightMap.height = cloudFiltered.height;
    frame.heightMap.data = fusedHeights; // move copy

    float minV = std::numeric_limits<float>::infinity();
    float maxV = -std::numeric_limits<float>::infinity();
    double sum = 0.0;
    for (float z : frame.heightMap.data) {
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

    auto tFrameEnd = std::chrono::steady_clock::now();

    if (metricsEnabled_) {
        using Fms = std::chrono::duration<float, std::milli>;
        lastStabilityMetrics_.frameId = frameCounter_;
        lastStabilityMetrics_.width = cloudFiltered.width;
        lastStabilityMetrics_.height = cloudFiltered.height;
        lastStabilityMetrics_.hardInvalid = lastValidationSummary_.invalid;
        lastStabilityMetrics_.buildMs = Fms(tBuildEnd - tBuildStart).count();
        lastStabilityMetrics_.filterMs = Fms(tFilterEnd - tFilterStart).count();
        lastStabilityMetrics_.fuseMs = Fms(tFuseEnd - tFuseStart).count();
        lastStabilityMetrics_.procTotalMs = Fms(tFrameEnd - tFrameStart).count();
        // Approx variance proxy: compute mean abs diff of neighbor pairs in final height map (quick O(N))
        double totalDiff = 0.0; uint32_t countDiff = 0; const auto& data = frame.heightMap.data;
        for (uint32_t y=0; y<frame.heightMap.height; ++y) {
            for (uint32_t x=1; x<frame.heightMap.width; ++x) {
                float a = data[y*frame.heightMap.width + x-1];
                float b = data[y*frame.heightMap.width + x];
                totalDiff += std::abs(a-b); ++countDiff;
            }
        }
        float meanAbsDiff = countDiff? static_cast<float>(totalDiff / countDiff):0.0f;
        // Exponential moving average to smooth variance proxy
        const float alpha = 0.1f;
        emaVariance_ = (emaVariance_==0.0f)? meanAbsDiff : (alpha*meanAbsDiff + (1.0f-alpha)*emaVariance_);
        lastStabilityMetrics_.avgVariance = emaVariance_;
        // Stability ratio: valid points minus those with high local diff (threshold) / valid
        uint32_t stable=0; uint32_t considered=0;
        const float diffThresh = meanAbsDiff * 1.5f + 1e-6f; // adaptive threshold
        for (uint32_t y=0; y<frame.heightMap.height; ++y) {
            for (uint32_t x=1; x<frame.heightMap.width; ++x) {
                float a = data[y*frame.heightMap.width + x-1];
                float b = data[y*frame.heightMap.width + x];
                if (std::isfinite(a) && std::isfinite(b)) {
                    ++considered; if (std::abs(a-b) <= diffThresh) ++stable;
                }
            }
        }
        lastStabilityMetrics_.stabilityRatio = considered? static_cast<float>(stable)/static_cast<float>(considered):1.0f;
        // Adaptive metrics capture
        lastStabilityMetrics_.adaptiveSpatial = adaptiveSpatialActive_ ? 1.0f : 0.0f;
        lastStabilityMetrics_.adaptiveStrong = (strongPass && applySpatial) ? 1.0f : 0.0f;
        lastStabilityMetrics_.adaptiveStreak = adaptiveSpatialActive_ ? unstableStreak_ : 0u;
        lastStabilityMetrics_.adaptiveTemporalBlend = adaptiveTemporalApplied ? 1.0f : 0.0f;
        // Store spatial variance ratio if sampled and meaningful
        if (sampled && preSpatialVar>0.0f && applySpatial) {
            lastStabilityMetrics_.spatialVarianceRatio = postSpatialVar>0.0f? (postSpatialVar / preSpatialVar):0.0f;
        } else {
            lastStabilityMetrics_.spatialVarianceRatio = 0.0f;
        }
    }

    // Persist filtered map for next-frame temporal blending
    if (adaptiveTemporalScale_ > 1.0f) {
        prevFilteredHeight_ = heightMap;
        prevFilteredValid_ = true;
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
