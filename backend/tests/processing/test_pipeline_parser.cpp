#include <gtest/gtest.h>
#include "processing/PipelineParser.h"

using namespace caldera::backend::processing;

TEST(PipelineParserTest, ParsesSimpleList) {
    auto res = parsePipelineSpec("build,plane_validate,temporal");
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.stages.size(), 3u);
    EXPECT_EQ(res.stages[0].name, "build");
    EXPECT_EQ(res.stages[1].name, "plane_validate");
    EXPECT_EQ(res.stages[2].name, "temporal");
}

TEST(PipelineParserTest, ParsesParamsAndLowercases) {
    auto res = parsePipelineSpec("spatial(mode=classic,passes=2,When=AdaptiveStrong),confidence(weights=0.5:0.3:0.2)");
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.stages.size(), 2u);
    EXPECT_EQ(res.stages[0].name, "spatial");
    auto& p = res.stages[0].params;
    ASSERT_EQ(p.count("mode"), 1u);
    EXPECT_EQ(p.at("mode"), "classic");
    ASSERT_EQ(p.count("passes"), 1u);
    EXPECT_EQ(p.at("passes"), "2");
    ASSERT_EQ(p.count("when"), 1u);
    EXPECT_EQ(p.at("when"), "AdaptiveStrong"); // value preserved case
    EXPECT_EQ(res.stages[1].name, "confidence");
    EXPECT_EQ(res.stages[1].params.at("weights"), "0.5:0.3:0.2");
}

TEST(PipelineParserTest, ErrorsOnMissingParen) {
    auto res = parsePipelineSpec("temporal(spatial=");
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.error.find("unmatched") != std::string::npos);
}

TEST(PipelineParserTest, ErrorsOnBadParam) {
    auto res = parsePipelineSpec("spatial(mode)");
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.error.find("param missing") != std::string::npos);
}

TEST(PipelineParserTest, EmptySpecRejected) {
    auto res = parsePipelineSpec("   \t  \n");
    EXPECT_FALSE(res.ok);
}
