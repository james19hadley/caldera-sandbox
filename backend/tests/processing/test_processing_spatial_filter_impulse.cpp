#include "processing/SpatialFilter.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>

using namespace caldera::backend::processing;

// Construct a 5x1 line: [0 0 10 0 0] -> after separable [1 2 1]/4 horizontally+vertically (vertical noop here),
// horizontal pass yields: idx2 (center) = (10*2 + neighbors*1 each)/ (2+1+1)= 20/4=5 -> but because we apply separable twice, for 1D row second pass identical.
// We'll validate central peak spreads and reduces.
TEST(SpatialFilterTest, Impulse1DSpread) {
    SpatialFilter filter(true);
    const int W=5, H=1;
    std::vector<float> data{0.f,0.f,10.f,0.f,0.f};
    filter.apply(data, W, H);
    // Expected after two separable passes effectively equivalent to single 1D convolution twice.
    // First pass (horizontal): [0, (0+0+10*2)/4=5, (0+10*2+0)/4=5, (10*2+0+0)/4=5, 0] but edges treat missing neighbors (renormalized) => we used code that renormalizes actual weights.
    // For x=1: values= [0,0,10] weights=1,2,1 =>10*2=20 /4=5
    // For x=2: [0,10,0] -> 20/4=5
    // For x=3: [10,0,0] -> 20/4=5
    // Second (vertical) no change (H=1)
    EXPECT_FLOAT_EQ(data[0], 0.f);
    // After two passes: center attenuates, neighbors get partial energy -> empirical result from separable implementation
    EXPECT_FLOAT_EQ(data[1], 2.5f);
    EXPECT_FLOAT_EQ(data[2], 5.f);
    EXPECT_FLOAT_EQ(data[3], 2.5f);
    EXPECT_FLOAT_EQ(data[4], 0.f);
}

TEST(SpatialFilterTest, NaNAwareSkip) {
    SpatialFilter filter(true);
    const int W=3, H=1;
    float nanv = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> data{0.f, nanv, 8.f};
    filter.apply(data, W, H);
    // Middle is NaN -> remains NaN
    EXPECT_TRUE(std::isnan(data[1]));
    // Ends: left uses neighbors [0,NaN] => weights used: center(2) + left(2? Actually position itself has w=2 and neighbor valid w=1) -> due to logic: at x=0 loop dx {-1,0,1}; -1 skipped, 0 valid (w=2), +1 is NaN skipped -> wsum=2 acc=0 => stays 0
    EXPECT_FLOAT_EQ(data[0], 0.f);
    // Right: neighbors [NaN,8] -> wsum=2 acc=16 => 8 remains 8
    EXPECT_FLOAT_EQ(data[2], 8.f);
}
