#pragma once
#include "processing/IHeightMapFilter.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <cstdlib>

namespace caldera::backend::processing {

// Simple separable spatial smoothing filter using kernel [1 2 1] / 4 in each direction.
// Phase M2: CPU reference implementation, NaN-aware (skips NaN neighbors; renormalizes by sum of weights actually used).
class SpatialFilter : public IHeightMapFilter {
public:
    explicit SpatialFilter(bool enableNaNAware = true)
        : nanAware_(enableNaNAware) {
    const char* alt = ::getenv("CALDERA_SPATIAL_KERNEL_ALT");
        if (alt) {
            std::string v(alt);
            if (v == "wide5") mode_ = Mode::Wide5;
        }
    }
    void apply(std::vector<float>& heightMap, int width, int height) override {
        if (width <= 0 || height <= 0) return;
        if (heightMap.size() != static_cast<size_t>(width*height)) return;
        if (scratch_.size() != heightMap.size()) scratch_.resize(heightMap.size());
        switch(mode_) {
            case Mode::Classic3: applySeparable(heightMap, width, height, 1); break;
            case Mode::Wide5: applySeparableWide5(heightMap, width, height); break;
        }
    }
private:
    enum class Mode { Classic3, Wide5 } mode_ = Mode::Classic3;
    bool nanAware_ = true;
    std::vector<float> scratch_;

    // radius=1 kernel [1 2 1]
    void applySeparable(std::vector<float>& buf, int w, int h, int radius) {
        // Horizontal
        for (int y=0; y<h; ++y) {
            int off = y*w;
            for (int x=0; x<w; ++x) {
                float c = buf[off+x];
                if (!std::isfinite(c)) { scratch_[off+x] = c; continue; }
                float acc=0.f, wsum=0.f;
                for(int dx=-radius; dx<=radius; ++dx){ int xx=x+dx; if(xx<0||xx>=w) continue; float v=buf[off+xx]; if(nanAware_ && !std::isfinite(v)) continue; float wgt = (dx==0)?2.f:1.f; acc+=v*wgt; wsum+=wgt; }
                scratch_[off+x] = wsum>0? acc/wsum : c;
            }
        }
        // Vertical
        for (int y=0; y<h; ++y) {
            for (int x=0; x<w; ++x) {
                float c = scratch_[y*w+x];
                if (!std::isfinite(c)) { buf[y*w+x] = c; continue; }
                float acc=0.f, wsum=0.f;
                for(int dy=-radius; dy<=radius; ++dy){ int yy=y+dy; if(yy<0||yy>=h) continue; float v=scratch_[yy*w+x]; if(nanAware_ && !std::isfinite(v)) continue; float wgt=(dy==0)?2.f:1.f; acc+=v*wgt; wsum+=wgt; }
                buf[y*w+x] = wsum>0? acc/wsum : c;
            }
        }
    }

    // radius=2 wide5 kernel [1 4 6 4 1] (normalized by sum=16)
    void applySeparableWide5(std::vector<float>& buf, int w, int h) {
        // Horizontal pass
        for(int y=0;y<h;++y){
            int off=y*w;
            for(int x=0;x<w;++x){
                float c=buf[off+x];
                if(!std::isfinite(c)){ scratch_[off+x]=c; continue; }
                float acc=0.f, wsum=0.f;
                for(int dx=-2; dx<=2; ++dx){ int xx=x+dx; if(xx<0||xx>=w) continue; float v=buf[off+xx]; if(nanAware_ && !std::isfinite(v)) continue; float wgt = (dx==0)?6.f: (std::abs(dx)==1?4.f:1.f); acc+=v*wgt; wsum+=wgt; }
                scratch_[off+x] = wsum>0? acc/wsum : c;
            }
        }
        // Vertical pass
        for(int y=0;y<h;++y){
            for(int x=0;x<w;++x){
                float c=scratch_[y*w+x];
                if(!std::isfinite(c)){ buf[y*w+x]=c; continue; }
                float acc=0.f, wsum=0.f;
                for(int dy=-2; dy<=2; ++dy){ int yy=y+dy; if(yy<0||yy>=h) continue; float v=scratch_[yy*w+x]; if(nanAware_ && !std::isfinite(v)) continue; float wgt = (dy==0)?6.f: (std::abs(dy)==1?4.f:1.f); acc+=v*wgt; wsum+=wgt; }
                buf[y*w+x] = wsum>0? acc/wsum : c;
            }
        }
    }
};

} // namespace caldera::backend::processing
