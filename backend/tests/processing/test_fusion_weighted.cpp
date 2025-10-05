#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

// Helper to build a vector
static std::vector<float> V(std::initializer_list<float> l){ return {l}; }

TEST(FusionAccumulatorWeighted, BasicWeightedAverageAndConfidence) {
    FusionAccumulator fusion;
    const int W=3,H=1; // 3 pixels
    // Heights layer A and B
    std::vector<float> hA = V({ 1.0f, 10.0f,  5.0f });
    std::vector<float> hB = V({ 3.0f,  2.0f, 20.0f });
    // Confidences: choose to weight toward A on pixel0 (A=0.8,B=0.2), toward B on pixel1 (A=0.1,B=0.9), equal on pixel2 (A=0.5,B=0.5)
    std::vector<float> cA = V({ 0.8f, 0.1f, 0.5f });
    std::vector<float> cB = V({ 0.2f, 0.9f, 0.5f });
    fusion.beginFrame(100,W,H);
    fusion.addLayer(FusionInputLayer{"A", hA.data(), cA.data(), W,H});
    fusion.addLayer(FusionInputLayer{"B", hB.data(), cB.data(), W,H});
    std::vector<float> outH; std::vector<float> outC;
    fusion.fuse(outH,&outC);
    ASSERT_EQ(outH.size(),3u);
    // Pixel0: (1*0.8 + 3*0.2)/(0.8+0.2)= (0.8 + 0.6)/1.0=1.4
    EXPECT_NEAR(outH[0], 1.4f, 1e-6f);
    // Pixel1: (10*0.1 + 2*0.9)/(1.0)= (1 + 1.8)=2.8
    EXPECT_NEAR(outH[1], 2.8f, 1e-6f);
    // Pixel2: equal weights -> (5 + 20)/2 = 12.5
    EXPECT_NEAR(outH[2], 12.5f, 1e-6f);
    // Confidence aggregation formula: sum(c^2)/sum(c)
    // Pixel0: (0.8^2 + 0.2^2)/(0.8+0.2)=(0.64+0.04)/1=0.68
    EXPECT_NEAR(outC[0], 0.68f, 1e-6f);
    // Pixel1: (0.1^2 + 0.9^2)/1 = (0.01+0.81)=0.82
    EXPECT_NEAR(outC[1], 0.82f, 1e-6f);
    // Pixel2: (0.25+0.25)/(1.0)=0.5
    EXPECT_NEAR(outC[2], 0.5f, 1e-6f);
    const auto& stats = fusion.stats();
    EXPECT_EQ(stats.strategy, 1); // weighted
    EXPECT_EQ(stats.fallbackMinZCount, 0u);
    EXPECT_EQ(stats.fusedValidCount, 3u);
}
