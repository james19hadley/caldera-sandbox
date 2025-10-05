#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>

using namespace caldera::backend::processing;

TEST(FusionAccumulatorTest, SingleLayerPassthrough) {
    FusionAccumulator fusion;
    const int W = 4, H = 3;
    std::vector<float> heights{ 0.0f, 0.1f, 0.2f, 0.3f,
                                 0.4f, 0.5f, 0.6f, 0.7f,
                                 0.8f, 0.9f, 1.0f, 1.1f };

    fusion.beginFrame(7, W, H);
    FusionInputLayer layer{ "sensorA", heights.data(), nullptr, W, H };
    fusion.addLayer(layer);

    std::vector<float> out;
    fusion.fuse(out, nullptr);

    ASSERT_EQ(out.size(), heights.size());
    for (size_t i=0;i<heights.size();++i) {
        EXPECT_FLOAT_EQ(out[i], heights[i]);
    }
}
