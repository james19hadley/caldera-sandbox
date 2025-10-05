#include "ProcessingManager.h"

#include <spdlog/logger.h>
#include <limits>
#include <cmath>
#include "processing/SpatialFilter.h"
#include "processing/FastGaussianBlur.h"
#include "tools/calibration/SensorCalibration.h"
#include <chrono>
#include <memory>
#include <string>
#include "processing/PipelineParser.h"

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
    // Strong kernel selection (planned extension): classic_double (default), wide5, fastgauss
    std::string strongKernel = "classic_double";
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_STRONG_KERNEL")) { strongKernel = e; }
    // store in adaptiveState_ placeholder for future stage refactor
    adaptiveState_.strongKernelChoice = strongKernel;
    // Adaptive temporal scale (optional) >1 means blend with previous filtered map when instability
    if(const char* e=std::getenv("CALDERA_ADAPTIVE_TEMPORAL_SCALE")) { try { float v=std::stof(e); if(v>1.0f && v<10.0f) adaptiveTemporalScale_ = v; } catch(...){} }

    // Confidence map env parsing (M5) with one-time / on-change logging for weights
    confidenceEnabled_ = [](){ if(const char* e=std::getenv("CALDERA_ENABLE_CONFIDENCE_MAP")) return std::atoi(e)==1; return false; }();
    exportConfidence_ = [](){ if(const char* e=std::getenv("CALDERA_PROCESSING_EXPORT_CONFIDENCE")) return std::atoi(e)==1; return false; }();
    {
        const char* w = std::getenv("CALDERA_CONFIDENCE_WEIGHTS");
        static std::string lastWeightsSeen; // cached raw string
        static bool loggedInvalidOnce = false;
        if (w && *w) {
            std::string cur(w);
            if (cur != lastWeightsSeen) {
                bool parsed = false;
                try {
                    // format: wS,wR,wT
                    size_t p1 = cur.find(',');
                    size_t p2 = p1==std::string::npos?std::string::npos:cur.find(',', p1+1);
                    if (p1!=std::string::npos && p2!=std::string::npos) {
                        float ws = std::stof(cur.substr(0,p1));
                        float wr = std::stof(cur.substr(p1+1, p2-(p1+1)));
                        float wt = std::stof(cur.substr(p2+1));
                        if (ws>0 && wr>=0 && wt>=0) {
                            float sum = ws+wr+wt; if (sum>0) { confWeightS_=ws; confWeightR_=wr; confWeightT_=wt; parsed = true; }
                        }
                    }
                } catch(...) { parsed = false; }
                if (parsed) {
                    if (orch_logger_) orch_logger_->info("Applied CALDERA_CONFIDENCE_WEIGHTS '{}' (S={:.3f} R={:.3f} T={:.3f})", cur, confWeightS_, confWeightR_, confWeightT_);
                } else {
                    if (orch_logger_ && !loggedInvalidOnce) {
                        orch_logger_->warn("Invalid CALDERA_CONFIDENCE_WEIGHTS '{}' , using defaults", cur);
                        loggedInvalidOnce = true;
                    }
                }
                lastWeightsSeen = cur;
            }
        }
    }
    if(const char* lo=std::getenv("CALDERA_CONFIDENCE_LOW")) { try { float v=std::stof(lo); if(v>=0 && v<=1) confLowThresh_=v; } catch(...){} }
    if(const char* hi=std::getenv("CALDERA_CONFIDENCE_HIGH")) { try { float v=std::stof(hi); if(v>=0 && v<=1) confHighThresh_=v; } catch(...){} }

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

    // M5 Step 2: parse pipeline env (no execution yet)
    parsePipelineEnv();

    // Stage instantiation (initial: temporal only) – will expand as we migrate logic
    if (height_filter_) {
        // TemporalStage will be recreated if filter replaced externally; simple strategy for now
        stages_.clear();
    }
}

void ProcessingManager::setWorldFrameCallback(WorldFrameCallback cb) { callback_ = std::move(cb); }

void ProcessingManager::processRawDepthFrame(const RawDepthFrame& raw) {
    // Phase 0 concurrency guard: multi-sensor tests can call this concurrently from different
    // SyntheticSensorDevice threads. The current implementation reuses internal member buffers
    // (fusion accumulator, prevFilteredHeight_, metrics state) and passes stack-owned temporary
    // height buffers into fusion layers by pointer, which is not thread-safe. A coarse mutex
    // ensures only one frame processes at a time until stage refactor introduces safe parallelism.
    std::lock_guard<std::mutex> lock(processMutex_);
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
    if (pipelineSpecValid_ && !parsedPipelineSpecs_.empty()) {
        if (orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) {
            orch_logger_->trace("Stage build: end (valid={} invalid={})", lastValidationSummary_.valid, lastValidationSummary_.invalid);
        }
    }

    // Prepare temp height map
    std::vector<float> heightMap;
    heightMap.reserve(cloudIn.points.size());
    for (const auto& p : cloudIn.points) {
        heightMap.push_back((p.valid && std::isfinite(p.z)) ? p.z : std::numeric_limits<float>::quiet_NaN());
    }

    bool usePipeline = pipelineSpecValid_ && !parsedPipelineSpecs_.empty();
    if (usePipeline && orch_logger_ && (frameCounter_ % 120)==0) {
        orch_logger_->info("Executing parsed pipeline ({} stages)", parsedPipelineSpecs_.size());
    }

    // Stage loop (initial temporal only). For now we still directly invoke legacy gating logic below.
    // Future: transform parsedPipelineSpecs_ into concrete stage objects and execute uniformly here.
    for (auto& stPtr : stages_) {
        (void)stPtr; // placeholder no-op until population logic added
    }

    // Apply temporal filter (either via pipeline spec or legacy path) with when= gating
    auto tFilterStart = std::chrono::steady_clock::now();
    bool temporalRequested = false;
    if (usePipeline) {
        for (const auto& stage : parsedPipelineSpecs_) if(stage.name=="temporal") { temporalRequested=true; break; }
        if (temporalRequested) {
            // Determine when= gating parameter (default always)
            std::string whenCond = "always";
            for (const auto& s : parsedPipelineSpecs_) if (s.name=="temporal") { auto it=s.params.find("when"); if(it!=s.params.end()) whenCond=it->second; break; }
            bool skip=false; const char* skipReason=nullptr;
            if (whenCond=="never") { skip=true; skipReason="when=never"; }
            else if (whenCond=="adaptive") { if(!(adaptiveSpatialActive_ || adaptiveMode_==2)) { skip=true; skipReason="adaptive inactive"; } }
            else if (whenCond=="adaptiveStrong") { if(!(adaptiveSpatialActive_ && adaptiveMode_==2)) { skip=true; skipReason="adaptive strong inactive"; } }
            else if (!(whenCond=="always")) { // unknown token -> skip with warning once
                skip=true; skipReason="unknown when condition";
                if(orch_logger_) orch_logger_->warn("temporal stage unknown when='{}' -> skipped", whenCond);
            }
            if(!skip) {
                if(orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) orch_logger_->trace("Stage temporal: start (when={})", whenCond);
                applyTemporalFilter(heightMap, cloudIn.width, cloudIn.height);
                if(orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) orch_logger_->trace("Stage temporal: end");
            } else if (orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) {
                orch_logger_->trace("Stage temporal: skipped ({})", skipReason);
            }
        }
    } else {
        applyTemporalFilter(heightMap, cloudIn.width, cloudIn.height);
    }

    // Optional spatial filter (M2) controlled by env CALDERA_ENABLE_SPATIAL_FILTER=1
    // NOTE: Previously this was parsed once into a static (baseSpatial). That caused later tests
    // in the same process (full suite run) to be unable to toggle the spatial filter dynamically
    // between test cases by setting/unsetting the env var. This broke the wide5 kernel tests which
    // rely on forcing deterministic single-pass behavior (disabling adaptive escalation) via envs
    // set just before constructing a new ProcessingManager. We now read the env each frame so
    // per-test env overrides take effect even after prior tests have run.
    int baseSpatial = 0; if(const char* e=std::getenv("CALDERA_ENABLE_SPATIAL_FILTER")) baseSpatial = std::atoi(e);
    bool pipelineRequestedSpatial = false;
    if (usePipeline) {
        for (const auto& s : parsedPipelineSpecs_) if (s.name=="spatial") { pipelineRequestedSpatial = true; break; }
        if (!pipelineRequestedSpatial) baseSpatial = 0; else baseSpatial = 1;
    }
    std::string spatialWhen="always";
    if (usePipeline && pipelineRequestedSpatial) {
        for (const auto& s: parsedPipelineSpecs_) if(s.name=="spatial") { auto it=s.params.find("when"); if(it!=s.params.end()) spatialWhen=it->second; break; }
        bool sSkip=false; const char* skipReason=nullptr;
        if(spatialWhen=="never") { sSkip=true; skipReason="when=never"; }
        else if(spatialWhen=="adaptive") { if(!(adaptiveMode_==2 && adaptiveSpatialActive_)) { sSkip=true; skipReason="adaptive inactive"; } }
        else if(spatialWhen=="adaptiveStrong") { if(!(adaptiveMode_==2 && adaptiveSpatialActive_ && unstableStreak_>= (uint32_t)adaptiveOnStreak_)) { sSkip=true; skipReason="adaptive strong inactive"; } }
        else if(!(spatialWhen=="always")) { sSkip=true; skipReason="unknown when condition"; if(orch_logger_) orch_logger_->warn("spatial stage unknown when='{}' -> skipped", spatialWhen); }
        if (sSkip) {
            baseSpatial = 0; // force disable spatial
            if(orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) orch_logger_->trace("Stage spatial: skipped ({})", skipReason);
        }
    }
    // Kernel selection (extended): classic(default), wide5 (alt classic impl), fastgauss (FastGaussianBlur)
    struct SpatialKernelBundle {
        std::unique_ptr<SpatialFilter> classic;
        std::unique_ptr<FastGaussianBlur> fastgauss;
    };
    auto& spatialKernelStore = []() -> SpatialKernelBundle& { static SpatialKernelBundle b; return b; }();
    auto acquireClassic = [&]() -> SpatialFilter& {
        if (!spatialKernelStore.classic) spatialKernelStore.classic = std::make_unique<SpatialFilter>(true);
        return *spatialKernelStore.classic;
    };
    auto acquireFastGauss = [&]() -> FastGaussianBlur& {
        if (!spatialKernelStore.fastgauss) {
            float sigma = 1.5f; // default
            if(const char* s=std::getenv("CALDERA_FASTGAUSS_SIGMA")) { try { float v=std::stof(s); if(v>0.1f && v<20.f) sigma=v; } catch(...){} }
            spatialKernelStore.fastgauss = std::make_unique<FastGaussianBlur>(sigma);
        }
        return *spatialKernelStore.fastgauss;
    };
    const char* altKernelEnv = std::getenv("CALDERA_SPATIAL_KERNEL_ALT");
    std::string altKernel = altKernelEnv? altKernelEnv : ""; // "", "wide5", "fastgauss"
    bool applySpatial = (baseSpatial == 1);
    bool strongPass = false;
    int sampleCount = 1024; if(const char* e=std::getenv("CALDERA_SPATIAL_SAMPLE_COUNT")) { int v=std::atoi(e); if(v>0) sampleCount = v; }
    SpatialApplyResult spatialRes{}; // capture spatial metrics if applied
    if (adaptiveMode_ == 2 && metricsEnabled_) {
        if (frameCounter_ > 0) {
            float stab = lastStabilityMetrics_.stabilityRatio;
            float varP = lastStabilityMetrics_.avgVariance;
            bool unstable = (stab < adaptiveStabilityMin_) || (varP > adaptiveVarianceMax_);
            if (unstable) { ++unstableStreak_; stableStreak_ = 0; }
            else { ++stableStreak_; unstableStreak_ = 0; }
            // Hysteresis toggling
            if (!adaptiveSpatialActive_ && unstableStreak_ >= (uint32_t)adaptiveOnStreak_) adaptiveSpatialActive_ = true;
            if (adaptiveSpatialActive_ && stableStreak_ >= (uint32_t)adaptiveOffStreak_) adaptiveSpatialActive_ = false;
            // Strong pass decision placeholder (future): strongPass derived from variance/stability thresholds
            strongPass = adaptiveSpatialActive_ && (varP > adaptiveStrongVarMult_ * adaptiveVarianceMax_ || stab < adaptiveStrongStabFrac_);
        }
    }
    if (applySpatial) {
        spatialRes = applySpatialFilter(heightMap, cloudIn.width, cloudIn.height, altKernel, applySpatial, strongPass, metricsEnabled_, sampleCount);
    }
    if (usePipeline && orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) {
        orch_logger_->trace("Stage spatial: end (strong={} preVar={:.6f} postVar={:.6f})", strongPass, spatialRes.preVar, spatialRes.postVar);
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

    // Fusion (Phase 0: single-sensor passthrough) - confidence not yet exported externally
    auto tFuseStart = std::chrono::steady_clock::now();
    if (usePipeline && orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) orch_logger_->trace("Stage fuse: start");
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
    if (usePipeline && orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) orch_logger_->trace("Stage fuse: end (w={} h={})", cloudFiltered.width, cloudFiltered.height);

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
        lastStabilityMetrics_.adaptiveStrong = (spatialRes.strong && spatialRes.applied) ? 1.0f : 0.0f;
        lastStabilityMetrics_.adaptiveStreak = adaptiveSpatialActive_ ? unstableStreak_ : 0u;
        lastStabilityMetrics_.adaptiveTemporalBlend = adaptiveTemporalApplied ? 1.0f : 0.0f;
        // Store spatial variance ratio if sampled and meaningful
        if (spatialRes.sampled && spatialRes.preVar>0.0f && spatialRes.applied) {
            lastStabilityMetrics_.spatialVarianceRatio = spatialRes.postVar>0.0f? (spatialRes.postVar / spatialRes.preVar):0.0f;
        } else lastStabilityMetrics_.spatialVarianceRatio = 0.0f;
        if (spatialRes.sampled && spatialRes.preEdge>0.0f && spatialRes.applied) {
            lastStabilityMetrics_.spatialEdgePreservationRatio = spatialRes.postEdge>0.0f? (spatialRes.postEdge / spatialRes.preEdge):0.0f;
        } else lastStabilityMetrics_.spatialEdgePreservationRatio = 0.0f;
        // Confidence map computation moved here (after current frame metrics available)
        if (confidenceEnabled_) {
            if (confidenceMap_.size() != heightMap.size()) confidenceMap_.assign(heightMap.size(), 0.0f);
            float S = lastStabilityMetrics_.stabilityRatio; if(S<0)S=0; else if(S>1)S=1;
            float R = lastStabilityMetrics_.spatialVarianceRatio; if(!(R>=0.f) || !std::isfinite(R) || R<=0.f) R=1.0f; if(R>2.f) R=1.0f;
            float T = lastStabilityMetrics_.adaptiveTemporalBlend; if(T<0)T=0; else if(T>1)T=1;
            float wS=confWeightS_, wR=confWeightR_, wT=confWeightT_;
            if (lastStabilityMetrics_.spatialVarianceRatio == 0.0f) wR = 0.0f;
            float wSum = wS+wR+wT; if(wSum<=0){ wS=1; wR=0; wT=0; wSum=1; }
            float invSum = 1.0f / wSum;
            float compS = wS * S;
            float compR = (wR>0)? wR*(1.0f - std::min(1.0f,std::max(0.0f,R))) : 0.0f;
            float compT = wT * T;
            double sumC=0.0; size_t n=confidenceMap_.size(); size_t lowCnt=0, highCnt=0;
            for(size_t i=0;i<n;++i){
                bool valid = std::isfinite(heightMap[i]);
                float c = 0.0f;
                if (valid) c = (compS + compR + compT) * invSum;
                if (c<0.f) c=0.f; else if (c>1.f) c=1.f;
                confidenceMap_[i]=c; sumC+=c; if(c<confLowThresh_)++lowCnt; else if(c>confHighThresh_)++highCnt;
            }
            lastStabilityMetrics_.meanConfidence = n? static_cast<float>(sumC/n):0.0f;
            lastStabilityMetrics_.fractionLowConfidence = n? static_cast<float>(lowCnt)/static_cast<float>(n):0.0f;
            lastStabilityMetrics_.fractionHighConfidence = n? static_cast<float>(highCnt)/static_cast<float>(n):0.0f;
            if (pipelineSpecValid_ && orch_logger_ && orch_logger_->should_log(spdlog::level::trace)) orch_logger_->trace("Stage confidence: mean={:.3f}", lastStabilityMetrics_.meanConfidence);
        } else {
            lastStabilityMetrics_.meanConfidence = 0.0f;
            lastStabilityMetrics_.fractionLowConfidence = 0.0f;
            lastStabilityMetrics_.fractionHighConfidence = 0.0f;
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
void ProcessingManager::applyTemporalFilter(std::vector<float>& heightMap, int w, int h) {
    if (height_filter_) height_filter_->apply(heightMap, w, h);
}

ProcessingManager::SpatialApplyResult ProcessingManager::applySpatialFilter(std::vector<float>& heightMap,
                                                                           int w,
                                                                           int h,
                                                                           const std::string& altKernel,
                                                                           bool applySpatial,
                                                                           bool strongPass,
                                                                           bool metricsEnabled,
                                                                           int sampleCount) {
    SpatialApplyResult res;
    if(!applySpatial) return res;
    res.applied = true;

    // Acquire kernels lazily (reuse legacy lambdas but localized here)
    struct SpatialKernelBundle { std::unique_ptr<SpatialFilter> classic; std::unique_ptr<FastGaussianBlur> fastgauss; };
    auto& spatialKernelStore = []() -> SpatialKernelBundle& { static SpatialKernelBundle b; return b; }();
    auto acquireClassic = [&]() -> SpatialFilter& {
        if (!spatialKernelStore.classic) spatialKernelStore.classic = std::make_unique<SpatialFilter>(true);
        return *spatialKernelStore.classic;
    };
    auto acquireFastGauss = [&]() -> FastGaussianBlur& {
        if (!spatialKernelStore.fastgauss) {
            float sigma = 1.5f; if(const char* s=std::getenv("CALDERA_FASTGAUSS_SIGMA")) { try { float v=std::stof(s); if(v>0.1f && v<20.f) sigma=v; } catch(...){} }
            spatialKernelStore.fastgauss = std::make_unique<FastGaussianBlur>(sigma);
        }
        return *spatialKernelStore.fastgauss;
    };

    // Pre-sampling
    std::vector<size_t> sampleIdx;
    if (metricsEnabled && sampleCount>0 && (int)heightMap.size() > sampleCount) {
        sampleIdx.reserve(sampleCount);
        size_t step = (heightMap.size() / sampleCount); if(step==0) step=1;
        size_t seed = (frameCounter_ * 1664525u + 1013904223u) % heightMap.size();
        size_t idx = seed % step;
        for (int i=0; i<sampleCount && idx<heightMap.size(); ++i, idx+=step) sampleIdx.push_back(idx);
        if(!sampleIdx.empty()) {
            double sumS=0.0,sumSq=0.0; int n=0; double edge=0.0; int edgeN=0;
            for(size_t si: sampleIdx){ float v=heightMap[si]; if(std::isfinite(v)){ sumS+=v; sumSq+=double(v)*v; ++n; } }
            if(n>1) res.preVar = static_cast<float>((sumSq - (sumS*sumS)/n)/(n-1));
            int W=w,H=h; for(size_t si: sampleIdx){ int y=int(si / W); int x=int(si % W); float c=heightMap[si]; if(!std::isfinite(c)) continue; float gx=0.f, gy=0.f; if(x+1<W){ float r=heightMap[si+1]; if(std::isfinite(r)) gx=r-c; } if(y+1<H){ float d=heightMap[si+W]; if(std::isfinite(d)) gy=d-c; } edge += std::fabs(gx)+std::fabs(gy); ++edgeN; }
            if(edgeN>0) res.preEdge = static_cast<float>(edge / edgeN);
            res.sampled = true;
        }
    }

    // Apply kernels
    if (altKernel == "fastgauss") {
        auto& fg = acquireFastGauss();
        fg.apply(heightMap, w, h);
        if (strongPass) {
            const std::string& sk = adaptiveState_.strongKernelChoice;
            if (sk == "classic_double" || sk == "fastgauss") {
                if (adaptiveStrongDoublePass_) fg.apply(heightMap, w, h);
            } else if (sk == "wide5") {
                auto& classic = acquireClassic();
                classic.apply(heightMap, w, h);
            }
        }
    } else {
        auto& classic = acquireClassic();
        classic.apply(heightMap, w, h);
        if (strongPass) {
            const std::string& sk = adaptiveState_.strongKernelChoice;
            if (sk == "classic_double") {
                if (adaptiveStrongDoublePass_) classic.apply(heightMap, w, h);
            } else if (sk == "wide5") {
                if (altKernel != "wide5") {
                    // fallback: apply another classic pass if cannot enforce wide5 distinctively
                    if (adaptiveStrongDoublePass_) classic.apply(heightMap, w, h);
                }
            } else if (sk == "fastgauss") {
                auto& fg = acquireFastGauss();
                fg.apply(heightMap, w, h);
            }
        }
    }
    res.strong = strongPass;

    // Post-sampling
    if (res.sampled) {
        double sumS=0.0,sumSq=0.0; int n=0; double edge=0.0; int edgeN=0; int W=w,H=h;
        for(size_t si=0; si<heightMap.size() && n>=0 && si<heightMap.size(); ++si) { /* guard not needed */ }
        // Use same sampleIdx indices
        for(size_t si: sampleIdx){ float v=heightMap[si]; if(std::isfinite(v)){ sumS+=v; sumSq+=double(v)*v; ++n; } }
        if(n>1) res.postVar = static_cast<float>((sumSq - (sumS*sumS)/n)/(n-1));
        for(size_t si: sampleIdx){ int y=int(si / W); int x=int(si % W); float c=heightMap[si]; if(!std::isfinite(c)) continue; float gx=0.f, gy=0.f; if(x+1<W){ float r=heightMap[si+1]; if(std::isfinite(r)) gx=r-c; } if(y+1<H){ float d=heightMap[si+W]; if(std::isfinite(d)) gy=d-c; } edge += std::fabs(gx)+std::fabs(gy); ++edgeN; }
        if(edgeN>0) res.postEdge = static_cast<float>(edge / edgeN);
    }
    return res;
}


} // namespace caldera::backend::processing

namespace caldera::backend::processing {

void ProcessingManager::parsePipelineEnv() {
    const char* spec = std::getenv("CALDERA_PROCESSING_PIPELINE");
    if(!spec || !*spec) { pipelineSpecValid_ = false; pipelineSpecError_ = "(unset)"; return; }
    auto res = parsePipelineSpec(spec);
    if(res.ok) {
        parsedPipelineSpecs_ = std::move(res.stages);
        pipelineSpecValid_ = true;
        pipelineSpecError_.clear();
        if(orch_logger_) {
            std::ostringstream oss; oss << "Parsed pipeline: ";
            bool first=true; for(auto& st: parsedPipelineSpecs_) { if(!first) oss << " -> "; first=false; oss << st.name; if(!st.params.empty()){ oss << "("; bool fp=true; for(auto& kv: st.params){ if(!fp) oss << ","; fp=false; oss << kv.first << "=" << kv.second; } oss << ")"; } }
            orch_logger_->info(oss.str());
        }
    } else {
        pipelineSpecValid_ = false;
        pipelineSpecError_ = res.error;
        if(orch_logger_) orch_logger_->warn("Failed to parse CALDERA_PIPELINE: {} (fallback to legacy sequence)", res.error);
    }
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
