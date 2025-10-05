#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

TEST(FusionAccumulatorMetrics, CollectsLayerAndFusedCounts) {
    FusionAccumulator fusion;
    const int W=3,H=2; size_t N=W*H;
    std::vector<float> a{0.0f,1.0f,2.0f, std::nanf(""), 4.0f, 5.0f}; // 5 finite
    std::vector<float> b{0.5f,std::nanf(""),2.5f, 3.5f, 4.5f, std::nanf("")}; // 4 finite
    fusion.beginFrame(10,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr, W,H});
    fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr, W,H});
    std::vector<float> out; fusion.fuse(out,nullptr);
    const auto& stats = fusion.stats();
    ASSERT_EQ(stats.layerCount, 2u);
    ASSERT_EQ(stats.layerValidCounts.size(), 2u);
    EXPECT_EQ(stats.layerValidCounts[0], 5u);
    EXPECT_EQ(stats.layerValidCounts[1], 4u);
    // Fused min-z valid count should be finite pixels where at least one layer finite.
    // Here: all 6 positions have at least one finite -> fusedValidCount == 6.
    EXPECT_EQ(stats.fusedValidCount, N);
    EXPECT_NEAR(stats.fusedValidRatio, 1.0f, 1e-6f);
}
