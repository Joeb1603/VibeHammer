#include <gtest/gtest.h>

#include "core/version.hpp"

TEST(Skeleton, CoreLibraryLinks) {
    EXPECT_EQ(wh40k::core::engine_version(), "0.1.0");
}
