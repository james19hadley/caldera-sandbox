#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>

using namespace caldera::backend::processing;

TEST(FusionAccumulatorConcat, PassthroughSingleSensor) {
    FusionAccumulator fusion;
    const int W=2, H=2;
    std::vector<float> a{1,2,3,4};
    fusion.beginFrame(1,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr, W, H});
    std::vector<float> out;
    fusion.fuse(out, nullptr);
    ASSERT_EQ(out.size(), a.size());
    EXPECT_FLOAT_EQ(out[0], 1);
    EXPECT_FLOAT_EQ(out[1], 2);
    EXPECT_FLOAT_EQ(out[2], 3);
    EXPECT_FLOAT_EQ(out[3], 4);
}

TEST(FusionAccumulatorConcat, ConcatTwoSensorsSameSize) {
    FusionAccumulator fusion;
    const int W=2, H=2;
    std::vector<float> a{1,2,3,4}; // row-major: [1 2]
                                   //            [3 4]
    std::vector<float> b{5,6,7,8}; //            [5 6]
                                   //            [7 8]
    fusion.beginFrame(1,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr, W, H});
    fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr, W, H});
    std::vector<float> out;
    fusion.fuse(out, nullptr);
    ASSERT_EQ(out.size(), 8u);
    // Expect: [1 2 5 6]
    //         [3 4 7 8]
    EXPECT_FLOAT_EQ(out[0], 1);
    EXPECT_FLOAT_EQ(out[1], 2);
    EXPECT_FLOAT_EQ(out[2], 5);
    EXPECT_FLOAT_EQ(out[3], 6);
    EXPECT_FLOAT_EQ(out[4], 3);
    EXPECT_FLOAT_EQ(out[5], 4);
    EXPECT_FLOAT_EQ(out[6], 7);
    EXPECT_FLOAT_EQ(out[7], 8);
}
