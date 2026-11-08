// M70: the --window flag (a window of a given size, for screenshots of the action bar at a small window). The bar's
// layout fix itself is drawing code in the executable (decision_panel.cpp, main.cpp) and has no logic to test.

#include <gtest/gtest.h>

#include <stdexcept>

#include "launch_options.hpp"

using namespace wh40k::client;

TEST(M70_LaunchOptions, WindowOpensAtTheGivenSize) {
    EXPECT_FALSE(parse_launch_args({}).window);
    const LaunchOptions o = parse_launch_args({"--window", "1024x700", "--screenshot", "out.png"});
    ASSERT_TRUE(o.window);
    EXPECT_EQ(*o.window, (CliWindow{1024, 700}));
    EXPECT_EQ(o.screenshot, "out.png");
    EXPECT_EQ(*parse_launch_args({"--window", "320x16384"}).window, (CliWindow{320, 16384}));
    for (const char* bad : {"1024", "1024x", "x700", "1024*700", "319x700", "1024x319", "16385x700", "-1024x700",
                            "1024x700x2", "1e3x700", "99999999x700", " 1024x700"}) {
        EXPECT_THROW((void)parse_launch_args({"--window", bad}), std::invalid_argument) << bad;
    }
}
