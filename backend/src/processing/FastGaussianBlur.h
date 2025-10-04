/*
 * FastGaussianBlur.h - Fast Gaussian blur filter implementing IHeightMapFilter
 * 
 * Based on Ivan Kutskir's linear-time algorithm: http://blog.ivank.net/fastest-gaussian-blur.html
 * Implementation by Basile Fraboni: https://github.com/bfraboni/FastGaussianBlur
 * 
 * Achieves O(n) complexity independent of blur radius using box blur approximation.
 */

#pragma once

#include "IHeightMapFilter.h"
#include <vector>

namespace caldera::backend::processing {

/**
 * FastGaussianBlur - IHeightMapFilter implementation using linear-time Gaussian blur
 * 
 * Uses box blur approximation (3 passes) to achieve near-perfect Gaussian blur
 * with O(n) complexity independent of blur radius.
 */
class FastGaussianBlur final : public IHeightMapFilter {
public:
    /**
     * Constructor with configurable blur strength
     * @param sigma Standard deviation of Gaussian kernel (default 1.5f for noise reduction)
     */
    explicit FastGaussianBlur(float sigma = 1.5f) : sigma_(sigma) {}
    
    // IHeightMapFilter interface
    void apply(std::vector<float>& data, int width, int height) override;

private:
    float sigma_;
    mutable std::vector<float> tempBuffer_; // Cached working buffer
    
    // Core algorithm functions based on reference implementation
    void std_to_box(int boxes[], float sigma, int n) const;
    void horizontal_blur(float* in, float* out, int w, int h, int r) const;
    void total_blur(float* in, float* out, int w, int h, int r) const;
    void box_blur(float*& in, float*& out, int w, int h, int r) const;
    void ensureBufferSize(size_t requiredSize) const;
};

} // namespace caldera::backend::processing