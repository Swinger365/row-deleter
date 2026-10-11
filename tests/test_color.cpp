/**
 * @file test_color.cpp
 * @brief Тесты цветовых функций и критерия «красности».
 */

#include "color.hpp"

#include <gtest/gtest.h>

using namespace redclean;

/**
 * @test Разбор RRGGBB и AARRGGBB; альфа-канал игнорируется.
 */
TEST(Color, ParsesHexWithAndWithoutAlpha)
{
    EXPECT_EQ(parseHexColor("FF0000"), (Rgb{255, 0, 0}));
    EXPECT_EQ(parseHexColor("FFFF0000"), (Rgb{255, 0, 0}));
    EXPECT_EQ(parseHexColor("00c00000"), (Rgb{0xC0, 0, 0}));
    EXPECT_EQ(parseHexColor("#12ab34"), (Rgb{0x12, 0xAB, 0x34}));
    EXPECT_FALSE(parseHexColor(""));
    EXPECT_FALSE(parseHexColor("FFF"));
    EXPECT_FALSE(parseHexColor("GG0000"));
}

TEST(Color, ToHex) { EXPECT_EQ(toHex(Rgb{0xC0, 0x0A, 0xFF}), "C00AFF"); }

TEST(Color, ToHsv)
{
    const Hsv red = toHsv({255, 0, 0});
    EXPECT_DOUBLE_EQ(red.h, 0.0);
    EXPECT_DOUBLE_EQ(red.s, 1.0);
    EXPECT_DOUBLE_EQ(red.v, 1.0);

    EXPECT_NEAR(toHsv({0, 255, 0}).h, 120.0, 1e-9);
    EXPECT_NEAR(toHsv({0, 0, 255}).h, 240.0, 1e-9);
    EXPECT_NEAR(toHsv({255, 0, 51}).h, 348.0, 1e-9);
    EXPECT_DOUBLE_EQ(toHsv({128, 128, 128}).s, 0.0);
}

/**
 * @test Осветлённый на 80% красный — бледно-розовый, затемнённый на 25% — тёмно-красный.
 */
TEST(Color, TintDarkensAndLightens)
{
    EXPECT_EQ(applyTint({255, 255, 255}, -0.5), (Rgb{128, 128, 128}));
    EXPECT_EQ(applyTint({0, 0, 0}, 0.5), (Rgb{128, 128, 128}));
    EXPECT_EQ(applyTint({255, 0, 0}, 0.0), (Rgb{255, 0, 0}));
    EXPECT_EQ(applyTint({255, 0, 0}, 0.8), (Rgb{255, 204, 204}));
    EXPECT_EQ(applyTint({255, 0, 0}, -0.25), (Rgb{191, 0, 0}));
}

TEST(Color, LegacyPalette)
{
    EXPECT_EQ(legacyIndexedColor(2), (Rgb{255, 0, 0}));
    EXPECT_EQ(legacyIndexedColor(10), (Rgb{255, 0, 0}));
    EXPECT_EQ(legacyIndexedColor(17), (Rgb{0, 0x80, 0}));
    EXPECT_FALSE(legacyIndexedColor(64));
    EXPECT_FALSE(legacyIndexedColor(65));
}

/**
 * @test Эвристика по умолчанию принимает красный, тёмно-красный Excel и слегка «грязный»
 *       красный; отвергает бледно-розовый (стиль «Плохой»), оранжевый accent2,
 *       жёлто-оранжевый и почти чёрный.
 */
TEST(RedCriteria, DefaultHeuristic)
{
    const RedCriteria criteria;
    EXPECT_TRUE(criteria.matches({0xFF, 0x00, 0x00}));
    EXPECT_TRUE(criteria.matches({0xC0, 0x00, 0x00}));
    EXPECT_TRUE(criteria.matches({0xE0, 0x20, 0x30}));
    EXPECT_FALSE(criteria.matches({0xFF, 0xC7, 0xCE}));
    EXPECT_FALSE(criteria.matches({0xED, 0x7D, 0x31}));
    EXPECT_FALSE(criteria.matches({0xFF, 0xC0, 0x00}));
    EXPECT_FALSE(criteria.matches({0x00, 0xFF, 0x00}));
    EXPECT_FALSE(criteria.matches({0x40, 0x00, 0x00}));
    EXPECT_FALSE(criteria.matches({0xFF, 0xFF, 0xFF}));
}

TEST(RedCriteria, ExactColorsOverrideHeuristic)
{
    RedCriteria criteria;
    criteria.exactColors = {Rgb{0xFF, 0x00, 0x00}};
    EXPECT_TRUE(criteria.matches({0xFF, 0x00, 0x00}));
    EXPECT_FALSE(criteria.matches({0xC0, 0x00, 0x00}));
}

TEST(RedCriteria, TunableThresholds)
{
    RedCriteria criteria;
    criteria.minSaturation = 0.1;
    EXPECT_TRUE(criteria.matches({0xFF, 0xC7, 0xCE}));
    criteria.maxHueDeviation = 30.0;
    EXPECT_TRUE(criteria.matches({0xED, 0x7D, 0x31}));
}
