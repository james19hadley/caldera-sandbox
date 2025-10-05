#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

static void makeLayer(std::vector<float>& buf, const std::initializer_list<float>& vals) { buf.assign(vals.begin(), vals.end()); }

TEST(FusionAccumulatorMinZ, TwoLayersMinSelection) {
    FusionAccumulator fusion;
    const int W=3,H=2; // 6 pixels
    std::vector<float> a,b;
    makeLayer(a,{ 0.5f, 1.0f,  2.0f,
                  3.5f, 4.0f,  5.0f });
    makeLayer(b,{ 0.6f, 0.9f,  2.5f,
                  3.0f, 10.0f, 1.0f });
    fusion.beginFrame(1,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr, W,H});
    fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr, W,H});
    std::vector<float> out; fusion.fuse(out,nullptr);
    ASSERT_EQ(out.size(), a.size());
    EXPECT_FLOAT_EQ(out[0], 0.5f); // min(0.5,0.6)
    EXPECT_FLOAT_EQ(out[1], 0.9f); // min(1.0,0.9)
    EXPECT_FLOAT_EQ(out[2], 2.0f); // min(2.0,2.5)
    EXPECT_FLOAT_EQ(out[3], 3.0f); // min(3.5,3.0)
    EXPECT_FLOAT_EQ(out[4], 4.0f); // min(4.0,10.0)
    EXPECT_FLOAT_EQ(out[5], 1.0f); // min(5.0,1.0)
}

TEST(FusionAccumulatorMinZ, NaNSkip) {
    FusionAccumulator fusion;
    const int W=2,H=2;
    std::vector<float> a{ std::nanf(""), 1.0f,
                          2.0f,          std::nanf("") };
    std::vector<float> b{ 0.5f,          std::nanf(""),
                          3.0f,          4.0f };
    fusion.beginFrame(2,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr, W,H});
    fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr, W,H});
    std::vector<float> out; fusion.fuse(out,nullptr);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_FLOAT_EQ(out[0], 0.5f); // only b valid
    EXPECT_FLOAT_EQ(out[1], 1.0f); // only a valid
    EXPECT_FLOAT_EQ(out[2], 2.0f); // min(2,3)
    EXPECT_FLOAT_EQ(out[3], 4.0f); // only b valid
}
