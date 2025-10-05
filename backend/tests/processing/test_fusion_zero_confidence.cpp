#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

TEST(FusionAccumulatorFallback, ZeroConfidenceTriggersMinZ) {
    FusionAccumulator fusion;
    const int W=3,H=1; // 3 pixels
    std::vector<float> hA{ 5.0f,  9.0f, 2.0f };
    std::vector<float> hB{ 7.0f,  1.0f, 8.0f };
    // Confidences all zero (and one negative to test clamp to 0) so weighted path should fallback per-pixel to min-z
    std::vector<float> cA{ 0.0f, -0.1f, 0.0f };
    std::vector<float> cB{ 0.0f,  0.0f, 0.0f };
    fusion.beginFrame(300,W,H);
    fusion.addLayer(FusionInputLayer{"A", hA.data(), cA.data(), W,H});
    fusion.addLayer(FusionInputLayer{"B", hB.data(), cB.data(), W,H});
    std::vector<float> outH; std::vector<float> outC;
    fusion.fuse(outH,&outC);
    // Expect pure min-z: min(5,7)=5, min(9,1)=1, min(2,8)=2
    EXPECT_FLOAT_EQ(outH[0],5.0f);
    EXPECT_FLOAT_EQ(outH[1],1.0f);
    EXPECT_FLOAT_EQ(outH[2],2.0f);
    // Confidence should be 0 because no positive weights contributed
    EXPECT_FLOAT_EQ(outC[0],0.0f);
    EXPECT_FLOAT_EQ(outC[1],0.0f);
    EXPECT_FLOAT_EQ(outC[2],0.0f);
    const auto& stats = fusion.stats();
    EXPECT_EQ(stats.strategy, 1); // started weighted strategy since confidence arrays exist
    // All three pixels used fallback min-z
    EXPECT_EQ(stats.fallbackMinZCount, 3u);
    EXPECT_EQ(stats.fusedValidCount, 3u);
}
