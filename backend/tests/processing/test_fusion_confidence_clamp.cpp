#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

TEST(FusionAccumulatorConfidence, ClampAboveOne) {
    FusionAccumulator fusion;
    const int W=2,H=1; // 2 pixels
    std::vector<float> hA{ 4.0f, 6.0f };
    std::vector<float> hB{ 8.0f, 2.0f };
    // Confidences exceed 1 and mixed
    std::vector<float> cA{ 1.5f,  2.2f }; // clamp to 1
    std::vector<float> cB{ 0.5f, 10.0f }; // second pixel clamp to 1
    fusion.beginFrame(400,W,H);
    fusion.addLayer(FusionInputLayer{"A", hA.data(), cA.data(), W,H});
    fusion.addLayer(FusionInputLayer{"B", hB.data(), cB.data(), W,H});
    std::vector<float> outH; std::vector<float> outC;
    fusion.fuse(outH,&outC);
    // After clamp: pixel0 weights: 1 and 0.5 -> fused height = (4*1 + 8*0.5)/(1.5) = (4 + 4)/1.5 = 5.3333
    EXPECT_NEAR(outH[0], 5.333333f, 1e-5f);
    // After clamp: pixel1 weights: 1 and 1 -> (6 + 2)/2 = 4
    EXPECT_NEAR(outH[1], 4.0f, 1e-6f);
    // Confidence pixel0: (1^2 + 0.5^2)/(1+0.5)= (1 + 0.25)/1.5 = 0.8333333
    EXPECT_NEAR(outC[0], 0.8333333f, 1e-5f);
    // Confidence pixel1: (1 + 1)/2 = 1
    EXPECT_NEAR(outC[1], 1.0f, 1e-6f);
    const auto& stats = fusion.stats();
    EXPECT_EQ(stats.strategy, 1);
    EXPECT_EQ(stats.fallbackMinZCount, 0u);
}
