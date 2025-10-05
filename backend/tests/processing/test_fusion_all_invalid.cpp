#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

TEST(FusionAccumulatorFallback, AllInvalid) {
    FusionAccumulator fusion;
    const int W=2,H=2; size_t N=W*H;
    std::vector<float> hA(N, std::nanf(""));
    std::vector<float> hB(N, std::nanf(""));
    std::vector<float> cA(N, 0.5f); // confidences irrelevant
    std::vector<float> cB(N, 0.7f);
    fusion.beginFrame(200,W,H);
    fusion.addLayer(FusionInputLayer{"A", hA.data(), cA.data(), W,H});
    fusion.addLayer(FusionInputLayer{"B", hB.data(), cB.data(), W,H});
    std::vector<float> outH; std::vector<float> outC;
    fusion.fuse(outH,&outC);
    for (auto v: outH) EXPECT_FLOAT_EQ(v, 0.0f); // our convention for empty
    for (auto c: outC) EXPECT_FLOAT_EQ(c, 0.0f); // confidence 0 when no finite
    const auto& stats = fusion.stats();
    EXPECT_EQ(stats.strategy, 1); // attempted weighted (confidence existed)
    EXPECT_EQ(stats.fusedValidCount, 0u);
    EXPECT_EQ(stats.fallbackEmptyCount, N);
}
