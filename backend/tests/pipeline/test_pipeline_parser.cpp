#include <gtest/gtest.h>
#include "processing/PipelineParser.h"

using namespace caldera::backend::processing;

TEST(PipelineParser, ParsesSimpleCommaList) {
    auto r = parsePipelineSpec("build,temporal,spatial,fusion");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.stages.size(), 4u);
    EXPECT_EQ(r.stages[0].name, "build");
    EXPECT_EQ(r.stages[1].name, "temporal");
    EXPECT_EQ(r.stages[2].name, "spatial");
    EXPECT_EQ(r.stages[3].name, "fusion");
}

TEST(PipelineParser, ParsesParamsAndLowercasesKeys) {
    auto r = parsePipelineSpec("spatial(kernel=fastGauss,Sample_Count=512),fusion");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.stages.size(), 2u);
    EXPECT_EQ(r.stages[0].name, "spatial");
    auto it = r.stages[0].params.find("kernel");
    ASSERT_NE(it, r.stages[0].params.end());
    EXPECT_EQ(it->second, "fastGauss");
    auto it2 = r.stages[0].params.find("sample_count");
    ASSERT_NE(it2, r.stages[0].params.end());
    EXPECT_EQ(it2->second, "512");
}

TEST(PipelineParser, RejectsInvalidChar) {
    auto r = parsePipelineSpec("bui|ld");
    ASSERT_FALSE(r.ok);
}
