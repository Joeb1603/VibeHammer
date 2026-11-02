#include <gtest/gtest.h>

#include <stdexcept>

#include "core/data/load_error.hpp"
#include "core/model/dice_expr.hpp"

using namespace wh40k::core;

TEST(R01_05_DiceExpr, ParsesFixedValue) {
    const DiceExpr d = parse_dice_expr("3");
    EXPECT_TRUE(d.is_fixed());
    EXPECT_EQ(d.fixed_value(), 3);
    EXPECT_EQ(d, DiceExpr::fixed(3));
}

TEST(R01_05_DiceExpr, ParsesD3AndD6) {
    EXPECT_EQ(parse_dice_expr("D3"), (DiceExpr{1, 3, 0}));
    EXPECT_EQ(parse_dice_expr("D6"), (DiceExpr{1, 6, 0}));
}

TEST(R01_05_DiceExpr, ParsesCountAndModifier) {
    EXPECT_EQ(parse_dice_expr("2D6"), (DiceExpr{2, 6, 0}));
    EXPECT_EQ(parse_dice_expr("D6+1"), (DiceExpr{1, 6, 1}));
    EXPECT_EQ(parse_dice_expr("2D6+3"), (DiceExpr{2, 6, 3}));
}

TEST(R01_05_DiceExpr, ToStringRoundTrips) {
    for (const char* s : {"0", "3", "D3", "D6", "2D6", "D6+1", "2D6+3"}) {
        EXPECT_EQ(to_string(parse_dice_expr(s)), s);
    }
}

TEST(R01_05_DiceExpr, MalformedThrowsDataError) {
    for (const char* s : {"", "D", "D4", "x", "D6+", "D6-1", "3D", "D6+1x", "0D6"}) {
        EXPECT_THROW((void)parse_dice_expr(s), DataError) << s;
    }
}

TEST(D002_RandomCharacteristics, FixedValueOfRandomExpressionIsProgrammerError) {
    EXPECT_THROW((void)parse_dice_expr("D6").fixed_value(), std::logic_error);
}
