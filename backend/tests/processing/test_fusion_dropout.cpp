#include "processing/FusionAccumulator.h"
#include <gtest/gtest.h>
#include <vector>
#include <cstdlib>

using namespace caldera::backend::processing;

static void fill(std::vector<float>& buf, float v, size_t n){ buf.assign(n,v); }

TEST(FusionAccumulatorDropout, AllActiveNoDropoutWindowZero) {
    ::setenv("CALDERA_FUSION_DROPOUT_WINDOW","0",1);
    FusionAccumulator fusion;
    const int W=2,H=2; size_t N=W*H;
    std::vector<float> a{1,2,3,4};
    std::vector<float> b{2,1,4,3};
    // Frame 1 both present
    fusion.beginFrame(1,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H});
    fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr,W,H});
    std::vector<float> out; fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().activeLayerCount, 2u);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 0u);
    // Frame 2 only A present; window==0 disables dropout so layer B absence should not count as stale yet (design: we only mark stale when diff>window, and window=0 means disabled, staleExcluded=0)
    fusion.beginFrame(2,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H});
    out.clear(); fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().activeLayerCount, 1u);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 0u);
}

TEST(FusionAccumulatorDropout, MarksStaleAfterWindow) {
    ::setenv("CALDERA_FUSION_DROPOUT_WINDOW","2",1); // stale if absent >2 frames
    FusionAccumulator fusion;
    const int W=1,H=1; size_t N=W*H; (void)N;
    std::vector<float> a{ 5.0f };
    std::vector<float> b{ 7.0f };
    // Frame 10 both
    fusion.beginFrame(10,W,H);
    fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H});
    fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr,W,H});
    std::vector<float> out; fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().activeLayerCount, 2u);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 0u);
    // Frame 11 only A
    fusion.beginFrame(11,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); out.clear(); fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().activeLayerCount, 1u);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 0u); // B absent 1 frame -> diff=1 <= window
    // Frame 12 only A
    fusion.beginFrame(12,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); out.clear(); fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 0u); // diff=2 == window -> still not stale ( > window )
    // Frame 13 only A
    fusion.beginFrame(13,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); out.clear(); fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 1u); // diff=3 > window -> B now stale
}

TEST(FusionAccumulatorDropout, RejoinClearsStale) {
    ::setenv("CALDERA_FUSION_DROPOUT_WINDOW","2",1);
    FusionAccumulator fusion;
    const int W=1,H=1; std::vector<float> a{1.0f}; std::vector<float> b{2.0f}; std::vector<float> out;
    // Establish both at frame 1
    fusion.beginFrame(1,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr,W,H}); fusion.fuse(out,nullptr);
    // Skip B for enough frames to become stale
    fusion.beginFrame(2,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); fusion.fuse(out,nullptr);
    fusion.beginFrame(3,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); fusion.fuse(out,nullptr);
    fusion.beginFrame(4,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); fusion.fuse(out,nullptr); // diff for B = 3 > 2 -> staleExcluded=1
    EXPECT_EQ(fusion.stats().staleExcludedCount, 1u);
    // Rejoin B at frame 5
    fusion.beginFrame(5,W,H); fusion.addLayer(FusionInputLayer{"A", a.data(), nullptr,W,H}); fusion.addLayer(FusionInputLayer{"B", b.data(), nullptr,W,H}); fusion.fuse(out,nullptr);
    EXPECT_EQ(fusion.stats().staleExcludedCount, 0u); // B now active again
    EXPECT_EQ(fusion.stats().activeLayerCount, 2u);
}
