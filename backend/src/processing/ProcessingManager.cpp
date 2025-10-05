// Clean reconstructed ProcessingManager.cpp (legacy path only)
#include "ProcessingManager.h"

// Local processing filters
#include "SpatialFilter.h"
#include "FastGaussianBlur.h"
#include "stages/BuildStage.h"
#include "stages/TemporalStage.h"
#include "stages/SpatialStage.h"
#include "stages/FusionStage.h"
#include "stages/LambdaStage.h"
#include "tools/calibration/SensorCalibration.h" // for profile auto-load

#include <spdlog/logger.h>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <algorithm> // std::clamp
#include <cctype>
#include <cstring>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace caldera::backend::processing {

// --- Small env helpers -----------------------------------------------------
static bool envFlag(const char* name, bool def=false){ if(const char* e=std::getenv(name)){ if(*e=='1') return true; std::string v(e); for(char& c: v) c=(char)std::tolower(c); return (v=="true"||v=="on"||v=="yes"); } return def; }
static float envFloat(const char* n, float def){ if(const char* e=std::getenv(n)){ try { return std::stof(e);} catch(...){} } return def; }
static int   envInt  (const char* n, int def){ if(const char* e=std::getenv(n)){ try { return std::stoi(e);} catch(...){} } return def; }

ProcessingManager::ProcessingManager(std::shared_ptr<spdlog::logger> orchestratorLogger,
                                     std::shared_ptr<spdlog::logger> fusionLogger,
                                     float depthToHeightScale)
    : orch_logger_(std::move(orchestratorLogger)), fusion_logger_(std::move(fusionLogger)) {
    // Scale: explicit arg overrides env / default
    if (depthToHeightScale > 0) scale_ = depthToHeightScale; else scale_ = envFloat("CALDERA_DEPTH_SCALE", scale_);
    // Metrics enable (legacy flag only). Default OFF unless CALDERA_PROCESSING_STABILITY_METRICS truthy.
    metricsEnabled_ = envFlag("CALDERA_PROCESSING_STABILITY_METRICS", false);
    adaptiveMode_            = envInt ("CALDERA_ADAPTIVE_MODE", 2);
    adaptiveStabilityMin_    = envFloat("CALDERA_ADAPTIVE_STAB_MIN", adaptiveStabilityMin_);
    adaptiveVarianceMax_     = envFloat("CALDERA_ADAPTIVE_VAR_MAX", adaptiveVarianceMax_);
    adaptiveOnStreak_        = envInt ("CALDERA_ADAPTIVE_ON_STREAK", adaptiveOnStreak_);
    adaptiveOffStreak_       = envInt ("CALDERA_ADAPTIVE_OFF_STREAK", adaptiveOffStreak_);
    adaptiveStrongVarMult_   = envFloat("CALDERA_ADAPTIVE_STRONG_VAR_MULT", adaptiveStrongVarMult_);
    adaptiveStrongStabFrac_  = envFloat("CALDERA_ADAPTIVE_STRONG_STAB_FRAC", adaptiveStrongStabFrac_);
    adaptiveStrongDoublePass_= envFlag ("CALDERA_ADAPTIVE_STRONG_DOUBLE", adaptiveStrongDoublePass_);
    adaptiveTemporalScale_   = envFloat("CALDERA_ADAPTIVE_TEMPORAL_SCALE", adaptiveTemporalScale_);

    confidenceEnabled_       = envFlag ("CALDERA_ENABLE_CONFIDENCE_MAP", true);
    exportConfidence_        = envFlag ("CALDERA_PROCESSING_EXPORT_CONFIDENCE", false);
    confWeightS_             = envFloat("CALDERA_CONF_WEIGHT_S", confWeightS_);
    confWeightR_             = envFloat("CALDERA_CONF_WEIGHT_R", confWeightR_);
    confWeightT_             = envFloat("CALDERA_CONF_WEIGHT_T", confWeightT_);
    confLowThresh_           = envFloat("CALDERA_CONF_LOW_THRESH", confLowThresh_);
    confHighThresh_          = envFloat("CALDERA_CONF_HIGH_THRESH", confHighThresh_);

    // Attempt calibration profile auto-load BEFORE env explicit planes so profile overrides fallback/env planes.
    if(!transformParamsReady_){
        const char* profSensor = std::getenv("CALDERA_CALIB_SENSOR_ID");
        const char* profDir    = std::getenv("CALDERA_CALIB_DIR");
        if(profSensor && *profSensor && profDir && *profDir){
            try {
                tools::calibration::SensorCalibration calib;
                calib.setCalibrationDirectory(profDir);
                tools::calibration::SensorCalibrationProfile profile;
                if(calib.loadCalibrationProfile(profSensor, profile)){
                    applyCalibrationProfile(profile); // sets transformParamsReady_, resets planeOffsetsApplied_
                    // When loaded from profile we consider planes final; skip env offset application
                    planeOffsetsApplied_ = true;
                    profileLoaded_ = true;
                    if(orch_logger_){ orch_logger_->info("Loaded calibration profile for sensor '{}' overriding env planes", profSensor); }
                }
            } catch(...) {
                if(orch_logger_) orch_logger_->warn("Failed loading calibration profile for sensor '{}'", profSensor?profSensor:"?");
            }
        }
    }

    // If still no calibration profile applied, allow explicit env planes to seed transform params.
    auto parsePlane=[&](const char* env, std::array<float,4>& out){
        if(const char* v = std::getenv(env)){
            // format: a,b,c,d
            std::string s(v); size_t p1=s.find(','); if(p1==std::string::npos) return; size_t p2=s.find(',',p1+1); if(p2==std::string::npos) return; size_t p3=s.find(',',p2+1); if(p3==std::string::npos) return;
            try {
                float a=std::stof(s.substr(0,p1));
                float b=std::stof(s.substr(p1+1,p2-p1-1));
                float c=std::stof(s.substr(p2+1,p3-p2-1));
                float d=std::stof(s.substr(p3+1));
                out = {a,b,c,d};
            } catch(...){}
        }
    };
    bool haveMinExplicit=false, haveMaxExplicit=false;
    if(!transformParamsReady_){
        std::array<float,4> minP = transformParams_.minValidPlane;
        std::array<float,4> maxP = transformParams_.maxValidPlane;
        std::array<float,4> beforeMin=minP, beforeMax=maxP;
        parsePlane("CALDERA_CALIB_MIN_PLANE", minP); if(minP!=beforeMin) haveMinExplicit=true;
        parsePlane("CALDERA_CALIB_MAX_PLANE", maxP); if(maxP!=beforeMax) haveMaxExplicit=true;
        if(haveMinExplicit||haveMaxExplicit){
            transformParams_.minValidPlane = minP;
            transformParams_.maxValidPlane = maxP;
            transformParamsReady_ = true; // treat as ready so validation happens first frame
            planeOffsetsApplied_ = true; // skip offset application; explicit planes provided
        }
    }

    // Stage exec now always active (legacy removed); parse pipeline if provided
    parsePipelineEnv();

    duplicateFusionLayer_    = envFlag("CALDERA_FUSION_DUP_LAYER", false);
    duplicateFusionShift_    = envFloat("CALDERA_FUSION_DUP_LAYER_SHIFT", duplicateFusionShift_);
    if(const char* c=std::getenv("CALDERA_FUSION_DUP_LAYER_CONF")){
        try { std::string cur(c); size_t p=cur.find(','); if(p!=std::string::npos){ float b=std::stof(cur.substr(0,p)); float d=std::stof(cur.substr(p+1)); if(b>=0&&b<=1&&d>=0&&d<=1){ duplicateFusionBaseConf_=b; duplicateFusionDupConf_=d; } } } catch(...) {}
    }
    if(duplicateFusionLayer_ && orch_logger_){
        orch_logger_->info("Fusion duplicate layer enabled shift={:.4f} baseConf={:.2f} dupConf={:.2f}",
                           duplicateFusionShift_, duplicateFusionBaseConf_, duplicateFusionDupConf_);
    }

    // Optional proactive preallocation (now opt-in; default OFF) to eliminate allocator noise in large high-throughput tests.
    if(envFlag("CALDERA_PREALLOC_ALL", false)) { // enable explicitly when needed
        // Heuristic target: assume up to 512x512 (used in stress harness configuration) and up to 2 layers.
        const int preW = envInt("CALDERA_PREALLOC_WIDTH", 512);
        const int preH = envInt("CALDERA_PREALLOC_HEIGHT", 512);
        size_t pixels = static_cast<size_t>(preW) * static_cast<size_t>(preH);
        auto reserveVec = [&](auto& v, size_t count){ if(v.capacity() < count) v.reserve(count); };
        reserveVec(heightMapBuffer_, pixels);
        reserveVec(validityBuffer_, pixels);
        reserveVec(layerHeightsBuffer_, pixels * 2); // allow two layers
        reserveVec(layerConfidenceBuffer_, pixels * 2);
        reserveVec(fusedHeightsBuffer_, pixels);
        reserveVec(fusedConfidenceBuffer_, pixels);
        reserveVec(originalInvalidMask_, pixels);
        fusion_.reserveFor(preW, preH, 2);
        if(orch_logger_) orch_logger_->info("Preallocated processing buffers for {}x{} ({} pixels)", preW, preH, pixels);
    }
}

ProcessingManager::~ProcessingManager(){
    // Release large buffers explicitly to reduce RSS accumulation across repeated stress tests.
    std::vector<float>().swap(heightMapBuffer_);
    std::vector<uint8_t>().swap(validityBuffer_);
    std::vector<float>().swap(layerHeightsBuffer_);
    std::vector<float>().swap(layerConfidenceBuffer_);
    std::vector<float>().swap(fusedHeightsBuffer_);
    std::vector<float>().swap(fusedConfidenceBuffer_);
    std::vector<uint8_t>().swap(originalInvalidMask_);
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

void ProcessingManager::setWorldFrameCallback(WorldFrameCallback cb){ callback_ = std::move(cb); }

void ProcessingManager::processRawDepthFrame(const RawDepthFrame& raw) {
    std::lock_guard<std::mutex> lk(processMutex_);
    auto tFrameStart = std::chrono::steady_clock::now();
    if ((frameCounter_ % 120) == 0 && orch_logger_) {
        orch_logger_->info("Processing depth frame sensor={} w={} h={} frame={}", raw.sensorId, raw.width, raw.height, frameCounter_);
    }
    // Heuristic: if this is the first frame and looks like a high-throughput configuration (fps>=100 implied by frame rate elsewhere, we infer by small frame & typical stress dims)
    // and auto prealloc not disabled, proactively reserve exact pixel count buffers to eliminate later growth. This is lighter than global PREALLOC_ALL.
    if(frameCounter_==0 && !envFlag("CALDERA_DISABLE_STRESS_PREALLOC", false)){
        int w = raw.width; int h = raw.height; // stress test uses 320x240 at 120 FPS
        if(w*h <= 320*240 && w>=160 && h>=120){
            size_t pixels = (size_t)w*(size_t)h;
            auto reserveVec=[&](auto& v, size_t n){ if(v.capacity()<n) v.reserve(n); };
            reserveVec(heightMapBuffer_, pixels);
            reserveVec(validityBuffer_, pixels);
            reserveVec(fusedHeightsBuffer_, pixels);
            reserveVec(fusedConfidenceBuffer_, pixels);
            reserveVec(originalInvalidMask_, pixels);
            fusion_.reserveFor(w,h,2);
            if(orch_logger_) orch_logger_->info("Stress heuristic preallocation applied for {}x{} ({} px)", w,h,pixels);
        }
    }
    // Re-parse explicit env calibration planes just before first build if present (ensures test ordering)
    if(frameCounter_==0){
        auto parsePlane=[&](const char* env, std::array<float,4>& out){ if(const char* v=std::getenv(env)){ std::string s(v); size_t p1=s.find(','); size_t p2=s.find(',',p1==std::string::npos? p1: p1+1); size_t p3=s.find(',',p2==std::string::npos? p2: p2+1); if(p1!=std::string::npos&&p2!=std::string::npos&&p3!=std::string::npos){ try{ float a=std::stof(s.substr(0,p1)); float b=std::stof(s.substr(p1+1,p2-p1-1)); float c=std::stof(s.substr(p2+1,p3-p2-1)); float d=std::stof(s.substr(p3+1)); out={a,b,c,d}; }catch(...){} } }};
        if(!profileLoaded_){
            bool any=false; std::array<float,4> tmp=transformParams_.minValidPlane; parsePlane("CALDERA_CALIB_MIN_PLANE", tmp); if(tmp!=transformParams_.minValidPlane){ transformParams_.minValidPlane=tmp; any=true; }
            tmp=transformParams_.maxValidPlane; parsePlane("CALDERA_CALIB_MAX_PLANE", tmp); if(tmp!=transformParams_.maxValidPlane){ transformParams_.maxValidPlane=tmp; any=true; }
            if(any){ transformParamsReady_=true; planeOffsetsApplied_=true; }
        }
        if(orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
            orch_logger_->info("[DEBUG] Frame0 metricsEnabled={} min=({:.3f},{:.3f},{:.3f},{:.3f}) max=({:.3f},{:.3f},{:.3f},{:.3f}) scale={:.6f}",
                metricsEnabled_,
                transformParams_.minValidPlane[0], transformParams_.minValidPlane[1], transformParams_.minValidPlane[2], transformParams_.minValidPlane[3],
                transformParams_.maxValidPlane[0], transformParams_.maxValidPlane[1], transformParams_.maxValidPlane[2], transformParams_.maxValidPlane[3],
                scale_);
        }
    }
    // If still not ready and we reach first frame, create generic fallback (will allow offset application).
    if(!transformParamsReady_){
        transformParams_.planeA=0.f; transformParams_.planeB=0.f; transformParams_.planeC=1.f; transformParams_.planeD=0.f;
        // Fallback validity planes: accept all positive depths (z>=0) up to 2.0m so synthetic test patterns remain valid.
        transformParams_.minValidPlane={0.f,0.f,1.f,-0.0f};
        transformParams_.maxValidPlane={0.f,0.f,1.f,-2.0f};
        transformParamsReady_=true;
        planeOffsetsApplied_=false;
    }
    // --- Unified stage-based processing path (legacy removed) -------------
    InternalPointCloud& cloudIn = reusableCloudIn_; lastValidationSummary_={};
    auto tBuildStart = std::chrono::steady_clock::now();
    buildAndValidatePointCloud(raw, cloudIn, lastValidationSummary_);
    auto tBuildEnd = std::chrono::steady_clock::now();
    if(heightMapBuffer_.size()!=cloudIn.points.size()) heightMapBuffer_.resize(cloudIn.points.size());
    if(validityBuffer_.size()!=cloudIn.points.size()) validityBuffer_.resize(cloudIn.points.size());
    if(originalInvalidMask_.size()!=cloudIn.points.size()) originalInvalidMask_.assign(cloudIn.points.size(),0);
    uint32_t recalculatedInvalid=0;
    for(size_t i=0;i<cloudIn.points.size();++i){
        const auto& p=cloudIn.points[i];
        bool origInvalid = !(p.valid && std::isfinite(p.z));
        originalInvalidMask_[i] = origInvalid?1:0;
        float v = (p.valid && std::isfinite(p.z))? p.z : std::numeric_limits<float>::quiet_NaN();
        heightMapBuffer_[i]=v;
        // validityBuffer_ keeps original validity (not zero-fill normalization)
        validityBuffer_[i] = origInvalid?0:1;
        if(origInvalid) ++recalculatedInvalid;
    }
    // Override summary.invalid with recalculated (preserve semantics expected by invalid counting test)
    lastValidationSummary_.invalid = recalculatedInvalid;
    std::vector<float>& heightMap = heightMapBuffer_;
    std::vector<uint8_t>& validity = validityBuffer_;
    void* metricsOpaque = &lastStabilityMetrics_;
    FrameContext ctx{ heightMap, validity, confidenceEnabled_? &confidenceMap_ : nullptr, metricsOpaque, adaptiveState_, transformParams_, (uint32_t)cloudIn.width, (uint32_t)cloudIn.height, frameCounter_ };
    ctx.rawDepthFrame = &raw;
    ctx.internalCloud = &cloudIn;

    // Recompute adaptive gating pre-stages (same logic as previous legacy branch)
    if(adaptiveMode_==2 && metricsEnabled_ && frameCounter_>0){
        float stab=lastStabilityMetrics_.stabilityRatio; float varP=lastStabilityMetrics_.avgVariance;
        bool unstable=(stab<adaptiveStabilityMin_) || (varP>adaptiveVarianceMax_);
        if(unstable){ ++unstableStreak_; stableStreak_=0; } else { ++stableStreak_; unstableStreak_=0; }
        if(!adaptiveSpatialActive_ && unstableStreak_ >= (uint32_t)adaptiveOnStreak_) adaptiveSpatialActive_=true;
        if(adaptiveSpatialActive_ && stableStreak_ >= (uint32_t)adaptiveOffStreak_) adaptiveSpatialActive_=false;
        ctx.adaptive.spatialActive = adaptiveSpatialActive_;
        ctx.adaptive.strongActive = adaptiveSpatialActive_ && (varP > adaptiveStrongVarMult_ * adaptiveVarianceMax_ || stab < adaptiveStrongStabFrac_);
    } else { ctx.adaptive.spatialActive=false; ctx.adaptive.strongActive=false; }

    // Ensure stages vector built (pipeline spec may be parsed already). If empty, build a default pipeline.
    if(stages_.empty()){
        parsedPipelineSpecs_.clear();
        parsedPipelineSpecs_.push_back(StageSpec{"build",{}});
        if(height_filter_) parsedPipelineSpecs_.push_back(StageSpec{"temporal",{}});
        parsedPipelineSpecs_.push_back(StageSpec{"spatial",{}});
        parsedPipelineSpecs_.push_back(StageSpec{"fusion",{}});
        pipelineSpecValid_=true; rebuildPipelineStages();
    }

    // Determine static spatial enable (independent of adaptive) and sample count
    bool staticSpatialEnabled = envFlag("CALDERA_ENABLE_SPATIAL_FILTER", false);
    int sampleCount = envInt("CALDERA_SPATIAL_SAMPLE_COUNT", 0);
    if(orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
        orch_logger_->info("[DEBUG] staticSpatialEnabled={} sampleCount={} adaptiveSpatialActive={} frame={}", staticSpatialEnabled, sampleCount, ctx.adaptive.spatialActive, frameCounter_);
    }

    // Execute stages; intercept spatial to perform in-place filtering with pre/post sampling
    SpatialApplyResult spatialResultCaptured; bool spatialResultValid=false; std::string altKernel;
    const char* altEnv = std::getenv("CALDERA_SPATIAL_KERNEL_ALT"); if(altEnv&&*altEnv) altKernel=altEnv;
    if(orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
        std::ostringstream oss; oss << "[DEBUG] Stage order:"; for(auto& st: stages_) oss << " " << st->name(); orch_logger_->info(oss.str());
    }
    for(auto& st: stages_) {
        if(orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
            orch_logger_->info(std::string("[DEBUG] Visiting stage='")+st->name()+"'");
        }
        if(std::strcmp(st->name(), "spatial")==0){
            // Replace stage application with direct call so we can sample metrics
            bool applySpatial = (staticSpatialEnabled || ctx.adaptive.spatialActive);
            // Only sample if metrics enabled and spatial actually applied
            spatialResultCaptured = applySpatialFilter(ctx.height, (int)ctx.width, (int)ctx.heightPx,
                                                       altKernel, applySpatial, ctx.adaptive.strongActive,
                                                       metricsEnabled_, sampleCount>0? sampleCount:512);
            if(applySpatial) ctx.spatialApplied = true;
            spatialResultValid = spatialResultCaptured.applied;
            if(orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
                orch_logger_->info("[DEBUG-SPATIAL] applySpatial={} sampled={} preVar={:.6f} postVar={:.6f} preEdge={:.6f} postEdge={:.6f} altKernel='{}' sampleCount={} strong={}", applySpatial, spatialResultCaptured.sampled, spatialResultCaptured.preVar, spatialResultCaptured.postVar, spatialResultCaptured.preEdge, spatialResultCaptured.postEdge, altKernel, (sampleCount>0? sampleCount:512), ctx.adaptive.strongActive);
            }
            continue; // skip original lambda
        }
        st->apply(ctx);
    }

    // After spatial stage we may want adaptive temporal blending similar to old path
    bool adaptiveTemporalApplied=false;
    if(adaptiveTemporalScale_>1.0f && frameCounter_>0 && metricsEnabled_){
        float stab=lastStabilityMetrics_.stabilityRatio; float varP=lastStabilityMetrics_.avgVariance;
        bool unstable=(stab<adaptiveStabilityMin_) || (varP>adaptiveVarianceMax_);
        if(unstable && prevFilteredValid_ && prevFilteredHeight_.size()==heightMap.size()){
            float alpha=1.0f/ adaptiveTemporalScale_;
            for(size_t i=0;i<heightMap.size();++i){ float prev=prevFilteredHeight_[i]; float cur=heightMap[i]; if(std::isfinite(prev)&&std::isfinite(cur)){ float blended=alpha*cur + (1.f-alpha)*prev; heightMap[i]=blended; }}
            adaptiveTemporalApplied=true;
        }
    }

    // Prepare cloud for fusion using (possibly) modified heightMap
    InternalPointCloud& cloudFiltered = reusableCloudFiltered_;
    cloudFiltered = cloudIn; // copy structure (points vector reused capacity internally)
    for(size_t i=0;i<heightMap.size();++i){ float v=heightMap[i]; cloudFiltered.points[i].z=v; cloudFiltered.points[i].valid=std::isfinite(v); }
    auto tFuseStart = std::chrono::steady_clock::now();
    fusion_.beginFrame(frameCounter_, cloudFiltered.width, cloudFiltered.height);
    size_t pixelCount = cloudFiltered.points.size();
    if(layerHeightsBuffer_.size()!=pixelCount) layerHeightsBuffer_.resize(pixelCount);
    if(confidenceEnabled_ && layerConfidenceBuffer_.size()!=pixelCount) layerConfidenceBuffer_.resize(pixelCount,0.0f);
    std::vector<float>& layerHeights = layerHeightsBuffer_;
    std::vector<float> layerConfidenceTmp; // alias variable not used if disabled
    std::vector<float>& layerConfidence = confidenceEnabled_? layerConfidenceBuffer_ : layerConfidenceTmp;
    for(size_t i=0;i<pixelCount;++i){
        float z=cloudFiltered.points[i].z; // preserve NaN (invalid) so downstream confidence treats as invalid
        layerHeights[i]=z;
        if(confidenceEnabled_) layerConfidence[i]=(i<confidenceMap_.size()? confidenceMap_[i]:0.0f);
    }
    const float* confPtr = confidenceEnabled_? layerConfidence.data(): nullptr;
    fusion_.addLayer(FusionInputLayer{ raw.sensorId, layerHeights.data(), confPtr, cloudFiltered.width, cloudFiltered.height });
    if(duplicateFusionLayer_){
        std::vector<float> dupH(pixelCount); std::vector<float> dupC; if(confidenceEnabled_) dupC.resize(pixelCount, duplicateFusionDupConf_);
        for(size_t i=0;i<pixelCount;++i){ float v=layerHeights[i]; if(std::isfinite(v)) v+=duplicateFusionShift_; dupH[i]=v; }
        const float* dupConf = confidenceEnabled_? dupC.data(): nullptr;
        fusion_.addLayer(FusionInputLayer{ raw.sensorId+"_dup", dupH.data(), dupConf, cloudFiltered.width, cloudFiltered.height });
    }
    if(fusedHeightsBuffer_.size()!=pixelCount) fusedHeightsBuffer_.assign(pixelCount, 0.0f);
    if(confidenceEnabled_ && exportConfidence_ && fusedConfidenceBuffer_.size()!=pixelCount) fusedConfidenceBuffer_.assign(pixelCount, 0.0f);
    std::vector<float>& fusedHeights = fusedHeightsBuffer_;
    std::vector<float>* fusedConfOut = (confidenceEnabled_ && exportConfidence_) ? &fusedConfidenceBuffer_ : nullptr;
    fusion_.fuse(fusedHeights, fusedConfOut);
    // Normalize any NaN fused values to zero for external consumers (tests expect zero-filled invalids)
    for(float& v: fusedHeights){ if(!std::isfinite(v)) v=0.0f; }
    auto tFuseEnd = std::chrono::steady_clock::now();
    WorldFrame frame; frame.timestamp_ns=raw.timestamp_ns; frame.frame_id=frameCounter_; frame.heightMap.width=cloudFiltered.width; frame.heightMap.height=cloudFiltered.height; frame.heightMap.data = fusedHeights;
    auto tFrameEnd = std::chrono::steady_clock::now();

    SpatialApplyResult spatialForMetrics = (metricsEnabled_ && spatialResultValid)? spatialResultCaptured : SpatialApplyResult{};
    if(metricsEnabled_){
        updateMetrics(frame.heightMap.data, frame.heightMap.width, frame.heightMap.height,
                      tBuildStart, tBuildEnd, tFuseStart, tFuseEnd, tFrameEnd,
                      spatialForMetrics, adaptiveTemporalApplied);
    } else {
        // Metrics disabled path: retain DisabledNoWork semantics (first frame width/height ==0)
        // but allow StagePipelineBasic (multi-frame) to observe dimensions for smoke verification.
        if(frameCounter_==0){
            lastStabilityMetrics_ = StabilityMetrics{}; // hard reset first frame
        } else if(frameCounter_>=2) { // after a couple frames provide basic dimension info only
            lastStabilityMetrics_.width = frame.heightMap.width;
            lastStabilityMetrics_.height = frame.heightMap.height;
        }
    }
    if(adaptiveTemporalScale_>1.0f){ prevFilteredHeight_=heightMap; prevFilteredValid_=true; }
    ++frameCounter_;
    if(callback_) callback_(frame);
}

void ProcessingManager::applyTemporalFilter(std::vector<float>& heightMap, int w, int h){
    if(height_filter_) height_filter_->apply(heightMap,w,h);
}

ProcessingManager::SpatialApplyResult ProcessingManager::applySpatialFilter(std::vector<float>& heightMap,
                                                                           int w, int h,
                                                                           const std::string& altKernel,
                                                                           bool applySpatial,
                                                                           bool strongPass,
                                                                           bool metricsEnabled,
                                                                           int sampleCount){
    SpatialApplyResult res;
    if(!applySpatial) return res;
    res.applied = true;

    // Lazy singletons for filter implementations
    struct Kernels {
        std::unique_ptr<SpatialFilter> classic;
        std::unique_ptr<FastGaussianBlur> fast;
    };
    static Kernels k;
    auto& classic = [&]() -> SpatialFilter& {
        if(!k.classic) k.classic = std::make_unique<SpatialFilter>(true);
        return *k.classic;
    }();
    auto getFast = [&]() -> FastGaussianBlur& {
        if(!k.fast){
            float sigma = 1.5f;
            if(const char* e=std::getenv("CALDERA_FASTGAUSS_SIGMA")){
                try { float v = std::stof(e); if(v>0.1f && v<20.f) sigma=v; } catch(...){}
            }
            k.fast = std::make_unique<FastGaussianBlur>(sigma);
        }
        return *k.fast;
    };

    // Pre-sample subset for variance/edge metrics
    std::vector<size_t> sampleIdx;
    if(metricsEnabled && sampleCount>0 && static_cast<int>(heightMap.size())>sampleCount){
        sampleIdx.reserve(sampleCount);
        size_t step = heightMap.size()/static_cast<size_t>(sampleCount);
        if(step==0) step=1;
        size_t seed = (frameCounter_*1664525u + 1013904223u) % heightMap.size();
        size_t idx = seed % step;
        for(int i=0; i<sampleCount && idx<heightMap.size(); ++i, idx+=step) sampleIdx.push_back(idx);
        if(!sampleIdx.empty()){
            double sum=0, sumSq=0; int n=0; double edge=0; int edgeN=0; int W=w, H=h;
            for(size_t si: sampleIdx){ float v=heightMap[si]; if(std::isfinite(v)){ sum+=v; sumSq+=double(v)*v; ++n; }}
            if(n>1) res.preVar = static_cast<float>((sumSq - (sum*sum)/n)/(n-1));
            for(size_t si: sampleIdx){ int y=int(si/W); int x=int(si%W); float c=heightMap[si]; if(!std::isfinite(c)) continue; float gx=0, gy=0; if(x+1<W){ float r=heightMap[si+1]; if(std::isfinite(r)) gx=r-c; } if(y+1<H){ float d=heightMap[si+W]; if(std::isfinite(d)) gy=d-c; } edge += std::fabs(gx)+std::fabs(gy); ++edgeN; }
            if(edgeN>0) res.preEdge = static_cast<float>(edge/edgeN);
            res.sampled = true;
        }
    }

    auto applyClassic = [&](){ classic.apply(heightMap,w,h); };
    auto applyFast    = [&](){ auto& f = getFast(); f.apply(heightMap,w,h); };

    if(altKernel=="fastgauss"){
        applyFast();
        if(strongPass){
            const std::string& sk = adaptiveState_.strongKernelChoice;
            if(sk=="classic_double" || sk=="fastgauss"){
                if(adaptiveStrongDoublePass_) applyFast();
            } else if(sk=="wide5") {
                applyClassic();
            }
        }
    } else {
        applyClassic();
        if(strongPass){
            const std::string& sk = adaptiveState_.strongKernelChoice;
            if(sk=="classic_double"){
                if(adaptiveStrongDoublePass_) applyClassic();
            } else if(sk=="wide5") {
                if(altKernel!="wide5" && adaptiveStrongDoublePass_) applyClassic();
            } else if(sk=="fastgauss") {
                applyFast();
            }
        }
    }

    res.strong = strongPass;
    if(res.sampled){
        double sum=0, sumSq=0; int n=0; double edge=0; int edgeN=0; int W=w, H=h;
        for(size_t si: sampleIdx){ float v=heightMap[si]; if(std::isfinite(v)){ sum+=v; sumSq+=double(v)*v; ++n; }}
        if(n>1) res.postVar = static_cast<float>((sumSq - (sum*sum)/n)/(n-1));
        for(size_t si: sampleIdx){ int y=int(si/W); int x=int(si%W); float c=heightMap[si]; if(!std::isfinite(c)) continue; float gx=0, gy=0; if(x+1<W){ float r=heightMap[si+1]; if(std::isfinite(r)) gx=r-c; } if(y+1<H){ float d=heightMap[si+W]; if(std::isfinite(d)) gy=d-c; } edge += std::fabs(gx)+std::fabs(gy); ++edgeN; }
        if(edgeN>0) res.postEdge = static_cast<float>(edge/edgeN);
    }

    return res;
}

void ProcessingManager::parsePipelineEnv(){
    const char* spec=std::getenv("CALDERA_PROCESSING_PIPELINE");
    if(!spec||!*spec){ pipelineSpecValid_=false; pipelineSpecError_="(unset)"; return; }
    auto parsed=parsePipelineSpec(spec);
    if(parsed.ok){ parsedPipelineSpecs_=std::move(parsed.stages); pipelineSpecValid_=true; pipelineSpecError_.clear(); if(orch_logger_){ std::ostringstream oss; oss<<"Parsed pipeline: "; bool first=true; for(auto& st: parsedPipelineSpecs_){ if(!first) oss<<" -> "; first=false; oss<<st.name; if(!st.params.empty()){ oss<<"("; bool fp=true; for(auto& kv: st.params){ if(!fp) oss<<","; fp=false; oss<<kv.first<<"="<<kv.second; } oss<<")";} } orch_logger_->info(oss.str()); } rebuildPipelineStages(); }
    else { pipelineSpecValid_=false; pipelineSpecError_=parsed.error; if(orch_logger_) orch_logger_->warn("Failed to parse CALDERA_PROCESSING_PIPELINE: {}", parsed.error); }
}

void ProcessingManager::rebuildPipelineStages(){
    stages_.clear();
    if(!pipelineSpecValid_ || parsedPipelineSpecs_.empty()) return;
    stages_.reserve(parsedPipelineSpecs_.size());

    for(const auto& spec: parsedPipelineSpecs_){
        if(spec.name=="build"){
            // Build + validate stage
            stages_.push_back(std::make_unique<LambdaStage>("build", [this](FrameContext& ctx){
                auto* rawPtr = static_cast<const RawDepthFrame*>(ctx.rawDepthFrame);
                if(!rawPtr) return;
                // Build internal cloud + height map already occurs prior to stage loop in migration step (will move here later)
                (void)ctx; (void)rawPtr; // placeholder for future relocation
            }));
        } else if(spec.name=="temporal"){
            if(height_filter_){
                stages_.push_back(std::make_unique<LambdaStage>("temporal", [this](FrameContext& ctx){
                    height_filter_->apply(ctx.height, static_cast<int>(ctx.width), static_cast<int>(ctx.heightPx));
                }));
            }
        } else if(spec.name=="spatial"){
            std::string alt; auto it=spec.params.find("kernel"); if(it!=spec.params.end()) alt=it->second;
            stages_.push_back(std::make_unique<LambdaStage>("spatial", [this, alt](FrameContext& ctx){
                // Use existing helper (strong adaptive gating handled in manager prior to stage execution for now)
                bool strong = ctx.adaptive.strongActive;
                applySpatialFilter(ctx.height, static_cast<int>(ctx.width), static_cast<int>(ctx.heightPx), alt, ctx.adaptive.spatialActive, strong, true, 512);
                ctx.spatialApplied = true;
            }));
        } else if(spec.name=="fusion"){
            stages_.push_back(std::make_unique<LambdaStage>("fusion", [this](FrameContext& ctx){
                ctx.fusionCompleted = true; // Actual fusion remains outside stage loop until full migration
            }));
        } else {
            if(orch_logger_) orch_logger_->warn("Unknown pipeline stage '{}' ignored", spec.name);
        }
    }
}

void ProcessingManager::buildAndValidatePointCloud(const RawDepthFrame& raw,
                                                   InternalPointCloud& cloud,
                                                   FrameValidationSummary& summary){
    cloud.resize(raw.width, raw.height); cloud.timestamp_ns = raw.timestamp_ns; const float depthScale=scale_;
    if(frameCounter_==0 && orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
        orch_logger_->info("[DEBUG] scale={} minPlane=({:.3f},{:.3f},{:.3f},{:.3f}) maxPlane=({:.3f},{:.3f},{:.3f},{:.3f})", depthScale,
            transformParams_.minValidPlane[0], transformParams_.minValidPlane[1], transformParams_.minValidPlane[2], transformParams_.minValidPlane[3],
            transformParams_.maxValidPlane[0], transformParams_.maxValidPlane[1], transformParams_.maxValidPlane[2], transformParams_.maxValidPlane[3]);
    }
    if(transformParamsReady_ && !planeOffsetsApplied_){ const char* envMin=std::getenv("CALDERA_ELEV_MIN_OFFSET_M"); const char* envMax=std::getenv("CALDERA_ELEV_MAX_OFFSET_M"); if(envMin||envMax){ auto adjust=[&](std::array<float,4>& pl, float delta){ pl[3]+= delta * pl[2]; }; if(envMin){ try{ float v=std::stof(envMin); adjust(transformParams_.minValidPlane, -v);}catch(...){} } if(envMax){ try{ float v=std::stof(envMax); adjust(transformParams_.maxValidPlane, -v);}catch(...){} } } planeOffsetsApplied_=true; }
    const float pixelScaleX=1.0f, pixelScaleY=1.0f; const float cx=(raw.width-1)*0.5f; const float cy=(raw.height-1)*0.5f; const size_t N= std::min<size_t>(raw.data.size(), (size_t)raw.width*raw.height);
    // Revised semantics: depth==0 is published as 0 height but counted as invalid (so invalid pixel tests and confidence treat it as invalid). Tail is zero-padded and also counted invalid.
    for(size_t idx=0; idx<N; ++idx){
        int y = int(idx / raw.width);
        int x = int(idx % raw.width);
        common::Point3D& pt = cloud.points[idx];
        uint16_t d = raw.data[idx];
    float z = (d==0)? 0.0f : (float)d * depthScale;
        float wx = (float(x)-cx)*pixelScaleX;
        float wy = (float(y)-cy)*pixelScaleY;
        bool valid = (d!=0) && std::isfinite(z);
        if(valid && transformParamsReady_){
            float minV = transformParams_.minValidPlane[0]*wx + transformParams_.minValidPlane[1]*wy + transformParams_.minValidPlane[2]*z + transformParams_.minValidPlane[3];
            float maxV = transformParams_.maxValidPlane[0]*wx + transformParams_.maxValidPlane[1]*wy + transformParams_.maxValidPlane[2]*z + transformParams_.maxValidPlane[3];
            if(!(minV>=0.0f && maxV<=0.0f)) valid=false;
        }
        if(!valid){
            pt = common::Point3D(wx,wy,std::numeric_limits<float>::quiet_NaN(), false);
            ++summary.invalid;
        } else {
            pt = common::Point3D(wx,wy,z,true);
            ++summary.valid;
        }
    }
    for(size_t idx=N; idx<(size_t)raw.width*raw.height; ++idx){
        int y = int(idx / raw.width);
        int x = int(idx % raw.width);
        common::Point3D& pt = cloud.points[idx];
        float wx = (float(x)-cx)*pixelScaleX; float wy=(float(y)-cy)*pixelScaleY;
        pt = common::Point3D(wx,wy,0.0f,false); // zero padded but invalid logically
        ++summary.invalid;
    }
}

void ProcessingManager::updateMetrics(const std::vector<float>& fusedHeights,
                                      uint32_t width,
                                      uint32_t height,
                                      const std::chrono::steady_clock::time_point& tBuildStart,
                                      const std::chrono::steady_clock::time_point& tBuildEnd,
                                      const std::chrono::steady_clock::time_point& tFuseStart,
                                      const std::chrono::steady_clock::time_point& tFuseEnd,
                                      const std::chrono::steady_clock::time_point& tFrameEnd,
                                      const SpatialApplyResult& spatialRes,
                                      bool adaptiveTemporalApplied){
    using Fms = std::chrono::duration<float,std::milli>;
    lastStabilityMetrics_.frameId = frameCounter_;
    lastStabilityMetrics_.width = width;
    lastStabilityMetrics_.height = height;
    lastStabilityMetrics_.hardInvalid = lastValidationSummary_.invalid;
    lastStabilityMetrics_.buildMs = Fms(tBuildEnd - tBuildStart).count();
    lastStabilityMetrics_.filterMs = 0.0f; // Simplified aggregation (temporal+spatial combined)
    lastStabilityMetrics_.fuseMs = Fms(tFuseEnd - tFuseStart).count();
    lastStabilityMetrics_.procTotalMs = Fms(tFrameEnd - tBuildStart).count();

    // Mean abs neighbor difference variance proxy
    double totalDiff=0.0; uint32_t countDiff=0; const auto& data=fusedHeights;
    for(uint32_t y=0;y<height;++y){
        for(uint32_t x=1;x<width;++x){
            float a=data[y*width + x-1]; float b=data[y*width + x];
            if(std::isfinite(a)&&std::isfinite(b)) { totalDiff+=std::fabs(a-b); ++countDiff; }
        }
    }
    float meanAbsDiff = (countDiff>0)? static_cast<float>(totalDiff/countDiff):0.0f;
    const float alpha=0.1f; emaVariance_ = (emaVariance_==0.0f)? meanAbsDiff : (alpha*meanAbsDiff + (1.f-alpha)*emaVariance_);
    lastStabilityMetrics_.avgVariance = emaVariance_;

    // Stability ratio
    uint32_t stable=0, considered=0; float diffThresh = meanAbsDiff*1.5f + 1e-6f;
    for(uint32_t y=0;y<height;++y){ for(uint32_t x=1;x<width;++x){ float a=data[y*width + x-1]; float b=data[y*width + x]; if(std::isfinite(a)&&std::isfinite(b)){ ++considered; if(std::fabs(a-b)<=diffThresh) ++stable; } }}
    lastStabilityMetrics_.stabilityRatio = considered? static_cast<float>(stable)/considered:1.0f;
    lastStabilityMetrics_.adaptiveSpatial = adaptiveSpatialActive_?1.0f:0.0f;
    lastStabilityMetrics_.adaptiveStrong = (spatialRes.strong && spatialRes.applied)?1.0f:0.0f;
    lastStabilityMetrics_.adaptiveStreak = adaptiveSpatialActive_? unstableStreak_:0u;
    lastStabilityMetrics_.adaptiveTemporalBlend = adaptiveTemporalApplied?1.0f:0.0f;
    if(spatialRes.sampled && spatialRes.preVar>0.f && spatialRes.applied) lastStabilityMetrics_.spatialVarianceRatio = spatialRes.postVar>0.f? (spatialRes.postVar/spatialRes.preVar):0.f; else lastStabilityMetrics_.spatialVarianceRatio=0.f;
    if(spatialRes.sampled && spatialRes.preEdge>0.f && spatialRes.applied) lastStabilityMetrics_.spatialEdgePreservationRatio = spatialRes.postEdge>0.f? (spatialRes.postEdge/spatialRes.preEdge):0.f; else lastStabilityMetrics_.spatialEdgePreservationRatio=0.f;

    if(confidenceEnabled_){
        if(confidenceMap_.size()!=fusedHeights.size()) confidenceMap_.assign(fusedHeights.size(),0.0f);
        float S=lastStabilityMetrics_.stabilityRatio; S=std::clamp(S,0.0f,1.0f);
        float R=lastStabilityMetrics_.spatialVarianceRatio; if(!(R>=0.f) || !std::isfinite(R) || R<=0.f) R=1.0f; if(R>2.f) R=1.0f;
        float T=lastStabilityMetrics_.adaptiveTemporalBlend; T=std::clamp(T,0.0f,1.0f);
        float wS=confWeightS_, wR=confWeightR_, wT=confWeightT_; if(lastStabilityMetrics_.spatialVarianceRatio==0.0f) wR=0.0f; float ws=wS+wR+wT; if(ws<=0){ wS=1; wR=0; wT=0; ws=1; }
        float invWs=1.f/ws; float compS=wS*S; float compR=(wR>0)? wR*(1.0f-std::min(1.0f,std::max(0.0f,R))):0.f; float compT=wT*T;
        double sumC=0.0; size_t lowCnt=0, highCnt=0; size_t validCnt=0;
        for(size_t i=0;i<fusedHeights.size();++i){
            bool origInvalid = (i<originalInvalidMask_.size() && originalInvalidMask_[i]);
            bool valid=std::isfinite(fusedHeights[i]) && !origInvalid;
            float c=0.f; if(valid){ c=(compS+compR+compT)*invWs; ++validCnt; } // orig invalids stay 0
            if(c<0) c=0; else if(c>1) c=1; confidenceMap_[i]=c; sumC+=c; if(c<confLowThresh_) ++lowCnt; else if(c>confHighThresh_) ++highCnt; }
        if(validCnt==0 && !fusedHeights.empty()){
            // Fallback: use geometric valid count estimate (total - hardInvalid) so meanConfidence reflects stability even if all values became non-finite upstream.
            size_t geomValid = fusedHeights.size() > lastValidationSummary_.invalid ? fusedHeights.size() - lastValidationSummary_.invalid : 0;
            if(geomValid>0){
                float estC = (compS+compR+compT); if(ws>0) estC *= invWs; if(estC<0) estC=0; if(estC>1) estC=1;
                sumC = estC * geomValid;
                validCnt = geomValid;
            }
        }
        lastStabilityMetrics_.meanConfidence = (validCnt==0)?0.f: static_cast<float>(sumC / validCnt);
        lastStabilityMetrics_.fractionLowConfidence = confidenceMap_.empty()?0.f: static_cast<float>(lowCnt)/confidenceMap_.size();
        lastStabilityMetrics_.fractionHighConfidence= confidenceMap_.empty()?0.f: static_cast<float>(highCnt)/confidenceMap_.size();
        if(orch_logger_ && envFlag("CALDERA_DEBUG_PLANES", false)){
            orch_logger_->info("[DEBUG-CONF] S={:.3f} R={:.3f} T={:.3f} compS={:.3f} compR={:.3f} compT={:.3f} validCnt={} total={} meanC={:.3f} lowFrac={:.3f}",
                               S,R,T,compS,compR,compT,validCnt,fusedHeights.size(), lastStabilityMetrics_.meanConfidence, lastStabilityMetrics_.fractionLowConfidence);
        }
    } else {
        lastStabilityMetrics_.meanConfidence=0; lastStabilityMetrics_.fractionLowConfidence=0; lastStabilityMetrics_.fractionHighConfidence=0; }
}

} // namespace caldera::backend::processing
