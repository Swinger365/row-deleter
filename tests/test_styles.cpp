/**
 * @file test_styles.cpp
 * @brief Тесты таблицы красных стилей.
 */

#include "styles.hpp"

#include "test_helpers.hpp"

#include <gtest/gtest.h>

using namespace redclean;
using namespace testutil;

namespace {

/// @brief Параметры по умолчанию с заданным объектом проверки.
StyleOptions withTarget(ColorTarget target)
{
    StyleOptions options;
    options.target = target;
    return options;
}

/// @brief Минимальная тема, где accent2 равен @p rgb.
std::string themeWithAccent2(const std::string& rgb)
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Office">
  <a:themeElements>
    <a:clrScheme name="Test">
      <a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1>
      <a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1>
      <a:dk2><a:srgbClr val="1F497D"/></a:dk2>
      <a:lt2><a:srgbClr val="EEECE1"/></a:lt2>
      <a:accent1><a:srgbClr val="4F81BD"/></a:accent1>
      <a:accent2><a:srgbClr val=")" + rgb + R"("/></a:accent2>
      <a:accent3><a:srgbClr val="9BBB59"/></a:accent3>
      <a:accent4><a:srgbClr val="8064A2"/></a:accent4>
      <a:accent5><a:srgbClr val="4BACC6"/></a:accent5>
      <a:accent6><a:srgbClr val="F79646"/></a:accent6>
      <a:hlink><a:srgbClr val="0000FF"/></a:hlink>
      <a:folHlink><a:srgbClr val="800080"/></a:folHlink>
    </a:clrScheme>
  </a:themeElements>
</a:theme>)";
}

/// @brief styles.xml с одним стилем, использующим заливку @p fillXml.
std::string singleFillStyles(const std::string& fillXml)
{
    return R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <fonts count="1"><font><color theme="1"/></font></fonts>
  <fills count="3">
    <fill><patternFill patternType="none"/></fill>
    <fill><patternFill patternType="gray125"/></fill>)" +
           fillXml + R"(
  </fills>
  <cellXfs count="2"><xf fontId="0" fillId="0"/><xf fontId="0" fillId="2"/></cellXfs>
</styleSheet>)";
}

}    // namespace

TEST(Styles, FillTargetDetectsRgbIndexedAndDarkRed)
{
    const auto table = RedStyleTable::build(testStylesXml(), "", withTarget(ColorTarget::Fill));
    ASSERT_EQ(table.styleCount(), kStyleCount);
    EXPECT_FALSE(table.isRed(kDefault));
    EXPECT_TRUE(table.isRed(kRedFill));
    EXPECT_FALSE(table.isRed(kGreenFill));
    EXPECT_FALSE(table.isRed(kRedFont));
    EXPECT_FALSE(table.isRed(kThemeOrange));
    EXPECT_TRUE(table.isRed(kIndexedRed));
    EXPECT_TRUE(table.isRed(kDarkRedFill));
    EXPECT_EQ(table.redCount(), 3u);
    EXPECT_FALSE(table.isRed(1000));
}

TEST(Styles, FontTarget)
{
    const auto table = RedStyleTable::build(testStylesXml(), "", withTarget(ColorTarget::Font));
    EXPECT_FALSE(table.isRed(kRedFill));
    EXPECT_TRUE(table.isRed(kRedFont));
    EXPECT_EQ(table.redCount(), 1u);
}

TEST(Styles, AnyTarget)
{
    const auto table = RedStyleTable::build(testStylesXml(), "", withTarget(ColorTarget::Any));
    EXPECT_TRUE(table.isRed(kRedFill));
    EXPECT_TRUE(table.isRed(kRedFont));
    EXPECT_EQ(table.redCount(), 4u);
}

TEST(Styles, ThemeColorsComeFromThemePart)
{
    const auto table = RedStyleTable::build(testStylesXml(), themeWithAccent2("FF0000"), withTarget(ColorTarget::Fill));
    EXPECT_TRUE(table.isRed(kThemeOrange));
}

/**
 * @test Палитра темы с перестановкой Excel: theme="0" — lt1, theme="1" — dk1.
 */
TEST(Styles, ThemePaletteParsing)
{
    const ThemePalette palette = parseThemePalette(themeWithAccent2("C0504D"));
    EXPECT_EQ(palette[0], (Rgb{0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(palette[1], (Rgb{0x00, 0x00, 0x00}));
    EXPECT_EQ(palette[2], (Rgb{0xEE, 0xEC, 0xE1}));
    EXPECT_EQ(palette[3], (Rgb{0x1F, 0x49, 0x7D}));
    EXPECT_EQ(palette[5], (Rgb{0xC0, 0x50, 0x4D}));
    EXPECT_EQ(parseThemePalette(""), defaultThemePalette());
}

/**
 * @test Оттенок применяется к цвету темы: accent2 = FF0000, осветлённый на 80%,
 *       даёт FFCCCC — это уже не красный.
 */
TEST(Styles, ThemeTintIsApplied)
{
    StyleOptions options;
    const auto light = RedStyleTable::build(
        singleFillStyles(R"(<fill><patternFill patternType="solid"><fgColor theme="5" tint="0.79998168889431442"/></patternFill></fill>)"),
        themeWithAccent2("FF0000"), options);
    EXPECT_FALSE(light.isRed(1));

    const auto dark = RedStyleTable::build(
        singleFillStyles(R"(<fill><patternFill patternType="solid"><fgColor theme="5" tint="-0.249977111117893"/></patternFill></fill>)"),
        themeWithAccent2("FF0000"), options);
    EXPECT_TRUE(dark.isRed(1));
}

TEST(Styles, CustomIndexedPaletteOverridesLegacy)
{
    std::string styles = testStylesXml();
    const std::string anchor = "<fonts";
    std::string palette = "<colors><indexedColors>";
    for (int i = 0; i < 64; ++i) palette += R"(<rgbColor rgb="FF00FF00"/>)";
    palette += "</indexedColors></colors>";
    styles.insert(styles.find(anchor), palette);

    const auto table = RedStyleTable::build(styles, "", StyleOptions{});
    EXPECT_FALSE(table.isRed(kIndexedRed));
    EXPECT_TRUE(table.isRed(kRedFill));
}

TEST(Styles, PatternAndGradientFills)
{
    StyleOptions options;
    EXPECT_TRUE(RedStyleTable::build(singleFillStyles(R"(<fill><patternFill patternType="darkGrid"><fgColor rgb="FF00FF00"/><bgColor rgb="FFFF0000"/></patternFill></fill>)"), "", options)
                    .isRed(1));
    EXPECT_FALSE(RedStyleTable::build(singleFillStyles(R"(<fill><patternFill patternType="solid"><fgColor rgb="FF00FF00"/><bgColor rgb="FFFF0000"/></patternFill></fill>)"), "", options)
                     .isRed(1));
    EXPECT_FALSE(RedStyleTable::build(singleFillStyles(R"(<fill><patternFill><fgColor rgb="FFFF0000"/></patternFill></fill>)"), "", options)
                     .isRed(1));
    EXPECT_TRUE(RedStyleTable::build(singleFillStyles(R"(<fill><gradientFill degree="90"><stop position="0"><color rgb="FFFFFFFF"/></stop><stop position="1"><color rgb="FFFF0000"/></stop></gradientFill></fill>)"), "", options)
                    .isRed(1));
    EXPECT_FALSE(RedStyleTable::build(singleFillStyles(R"(<fill><patternFill patternType="solid"><fgColor auto="1"/></patternFill></fill>)"), "", options)
                     .isRed(1));
}

TEST(Styles, PrefixedNamespaceIsSupported)
{
    const std::string styles = R"(<x:styleSheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <x:fonts><x:font/></x:fonts>
  <x:fills><x:fill><x:patternFill patternType="none"/></x:fill><x:fill><x:patternFill patternType="solid"><x:fgColor rgb="FFFF0000"/></x:patternFill></x:fill></x:fills>
  <x:cellXfs><x:xf fontId="0" fillId="0"/><x:xf fontId="0" fillId="1"/></x:cellXfs>
</x:styleSheet>)";
    const auto table = RedStyleTable::build(styles, "", StyleOptions{});
    EXPECT_FALSE(table.isRed(0));
    EXPECT_TRUE(table.isRed(1));
}

TEST(Styles, EmptyStylesMeansNothingIsRed)
{
    const auto table = RedStyleTable::build("", "", StyleOptions{});
    EXPECT_EQ(table.styleCount(), 0u);
    EXPECT_FALSE(table.isRed(0));
}

TEST(Styles, MalformedXmlThrows)
{
    EXPECT_THROW(RedStyleTable::build("<styleSheet><fills>", "", StyleOptions{}), std::runtime_error);
}
