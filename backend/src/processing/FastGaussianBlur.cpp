/*
 * FastGaussianBlur.cpp - Linear-time Gaussian blur implementation
 * Based on Ivan Kutskir's algorithm with Basile Fraboni's improvements
 */

#include "FastGaussianBlur.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace caldera::backend::processing {

void FastGaussianBlur::apply(std::vector<float>& data, int width, int height) {
    if (data.empty() || width <= 0 || height <= 0 || sigma_ <= 0.0f) {
        return; // No-op for invalid input, following ProcessingManager style
    }
    
    const size_t expectedSize = static_cast<size_t>(width) * height;
    if (data.size() != expectedSize) {
        return; // Size mismatch, no-op
    }
    
    // Skip blur for trivial cases where blur would have no effect
    if (width == 1 && height == 1) {
        return; // Single pixel, no blur needed
    }
    
    // Ensure we have working buffers 
    ensureBufferSize(expectedSize);
    
    float* in = data.data();
    float* out = tempBuffer_.data();
    
    // Calculate box blur parameters for 3-pass approximation using reference algorithm
    int boxes[3];
    std_to_box(boxes, sigma_, 3);
    
    // Clamp radii for very small images to avoid overlap in vertical pass loops
    // The Ivan Kutskir box blur formulation assumes r < min(w,h)/2; when this is violated
    // (e.g. tiny synthetic test frames) the vertical pass write loops overlap and can
    // write one row past the buffer (observed ASAN heap-buffer-overflow). We clamp here
    // rather than modifying inner loops to preserve algorithmic structure.
    int maxR = static_cast<int>((std::min(width, height) - 1) / 2); // ensure at least one untouched band
    if (maxR < 0) maxR = 0;
    for (int k = 0; k < 3; ++k) {
        if (boxes[k] > maxR) boxes[k] = maxR;
    }

    // If all radii collapse to 0 no blur effect is needed.
    if (boxes[0] == 0 && boxes[1] == 0 && boxes[2] == 0) {
        return;
    }

    // Perform box blur passes using reference implementation (note alternating in/out)
    box_blur(in, out, width, height, boxes[0]);
    box_blur(out, in, width, height, boxes[1]);
    box_blur(in, out, width, height, boxes[2]);
    
    // Copy result back to input vector if needed
    if (out != data.data()) {
        std::memcpy(data.data(), out, expectedSize * sizeof(float));
    }
}

void FastGaussianBlur::std_to_box(int boxes[], float sigma, int n) const {
    // ideal filter width
    float wi = std::sqrt((12*sigma*sigma/n)+1);  
    int wl = std::floor(wi);  
    if(wl%2==0) wl--;
    int wu = wl+2;
    
    float mi = (12*sigma*sigma - n*wl*wl - 4*n*wl - 3*n)/(-4*wl - 4);
    int m = std::round(mi);
    
    for(int i=0; i<n; i++)  
        boxes[i] = ((i < m ? wl : wu) - 1) / 2;
}

void FastGaussianBlur::horizontal_blur(float* in, float* out, int w, int h, int r) const {
    float iarr = 1.f / (r+r+1);
    #pragma omp parallel for
    for(int i=0; i<h; i++) {
        int ti = i*w, li = ti, ri = ti+r;
        float fv = in[ti], lv = in[ti+w-1], val = (r+1)*fv;
        
        for(int j=0; j<r; j++) val += in[ti+j];
        for(int j=0; j<=r; j++) { val += in[ri++] - fv; out[ti++] = val*iarr; }
        for(int j=r+1; j<w-r; j++) { val += in[ri++] - in[li++]; out[ti++] = val*iarr; }
        for(int j=w-r; j<w; j++) { val += lv - in[li++]; out[ti++] = val*iarr; }
    }
}

void FastGaussianBlur::total_blur(float* in, float* out, int w, int h, int r) const {
    float iarr = 1.f / (r+r+1);
    #pragma omp parallel for
    for(int i=0; i<w; i++) {
        int ti = i, li = ti, ri = ti+r*w;
        float fv = in[ti], lv = in[ti+w*(h-1)], val = (r+1)*fv;
        
        for(int j=0; j<r; j++) val += in[ti+j*w];
        for(int j=0; j<=r; j++) { val += in[ri] - fv; out[ti] = val*iarr; ri+=w; ti+=w; }
        for(int j=r+1; j<h-r; j++) { val += in[ri] - in[li]; out[ti] = val*iarr; li+=w; ri+=w; ti+=w; }
        for(int j=h-r; j<h; j++) { val += lv - in[li]; out[ti] = val*iarr; li+=w; ti+=w; }
    }
}

void FastGaussianBlur::box_blur(float*& in, float*& out, int w, int h, int r) const {
    std::swap(in, out);
    horizontal_blur(out, in, w, h, r);
    total_blur(in, out, w, h, r);
}

void FastGaussianBlur::ensureBufferSize(size_t requiredSize) const {
    if (tempBuffer_.size() < requiredSize) {
        tempBuffer_.resize(requiredSize);
    }
}

} // namespace caldera::backend::processing