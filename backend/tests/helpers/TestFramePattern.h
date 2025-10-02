#pragma once
#include <cstdint>
#include "common/DataTypes.h"

// Utility to fill a WorldFrame height map with a deterministic pseudo-random pattern.
// Purpose: ensure per-frame checksum variability while remaining fast and reproducible.
// Algorithm: xorshift32 seeded from frame_id to avoid cross-frame correlation reuse.
namespace caldera::backend::tests {

inline void fillDeterministicPattern(caldera::backend::common::WorldFrame &wf, uint64_t frame_id) {
    using caldera::backend::common::WorldFrame;
    uint32_t seed = static_cast<uint32_t>((frame_id * 2654435761u) ^ 0xA5A5A5A5u);
    float *ptr = wf.heightMap.data.data();
    size_t n = wf.heightMap.data.size();
    for (size_t i=0; i<n; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        ptr[i] = (seed & 0xFFFFFF) * (1.0f / 16777216.0f); // map to [0,1)
    }
}

} // namespace caldera::backend::tests
