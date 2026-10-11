/**
 * @file test_cli.cpp
 * @brief Тесты разбора командной строки.
 */

#include "cli.hpp"

#include <gtest/gtest.h>

using namespace redclean;

TEST(Cli, ParsesFullCommandLine)
{
    const CliCommand cmd = parseCommandLine({"-i", "in.xlsx", "-o", "out.xlsx", "-s", "Лист1", "--sheet", "Данные:7",
                                             "-r", "3", "-j", "8", "--target=any", "--colors", "FF0000,#c00000", "-v"});
    const CleanOptions& o = cmd.options;
    EXPECT_EQ(o.inputPath, "in.xlsx");
    EXPECT_EQ(o.outputPath, "out.xlsx");
    ASSERT_EQ(o.sheets.size(), 2u);
    EXPECT_EQ(o.sheets[0].name, "Лист1");
    EXPECT_FALSE(o.sheets[0].startRow);
    EXPECT_EQ(o.sheets[1].name, "Данные");
    EXPECT_EQ(o.sheets[1].startRow, 7u);
    EXPECT_EQ(o.defaultStartRow, 3u);
    EXPECT_EQ(o.threads, 8u);
    EXPECT_EQ(o.style.target, ColorTarget::Any);
    EXPECT_EQ(o.style.criteria.exactColors, (std::vector<Rgb>{{0xFF, 0, 0}, {0xC0, 0, 0}}));
    EXPECT_TRUE(cmd.verbose);
    EXPECT_FALSE(o.dryRun);
}

TEST(Cli, PositionalInputAndInPlace)
{
    const CliCommand cmd = parseCommandLine({"book.xlsx", "--in-place", "--all-sheets"});
    EXPECT_EQ(cmd.options.inputPath, "book.xlsx");
    EXPECT_EQ(cmd.options.outputPath, "book.xlsx");
    EXPECT_TRUE(cmd.options.allSheets);
}

TEST(Cli, DryRunDoesNotNeedOutput)
{
    const CliCommand cmd = parseCommandLine({"-i", "a.xlsx", "-s", "S", "--dry-run"});
    EXPECT_TRUE(cmd.options.dryRun);
    EXPECT_TRUE(cmd.options.outputPath.empty());
}

TEST(Cli, HeuristicThresholds)
{
    const CliCommand cmd = parseCommandLine(
        {"-i", "a.xlsx", "-s", "S", "--dry-run", "--hue-tolerance", "30", "--min-saturation=0.2", "--min-value", "0.3"});
    EXPECT_DOUBLE_EQ(cmd.options.style.criteria.maxHueDeviation, 30.0);
    EXPECT_DOUBLE_EQ(cmd.options.style.criteria.minSaturation, 0.2);
    EXPECT_DOUBLE_EQ(cmd.options.style.criteria.minValue, 0.3);
}

TEST(Cli, Help)
{
    EXPECT_TRUE(parseCommandLine({"--help"}).showHelp);
    EXPECT_NE(usageText("prog").find("--sheet"), std::string::npos);
}

/**
 * @test Некорректная командная строка: в том числе нет листов, нет выходного файла,
 *       у опции нет значения.
 */
TEST(Cli, Errors)
{
    EXPECT_THROW(parseCommandLine({}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-o", "b.xlsx"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o", "b", "-r", "0"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o", "b", "-r", "abc"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S:x", "-o", "b"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", ":5", "-o", "b"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o", "b", "--target", "border"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o", "b", "--colors", "red"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o", "b", "--bogus"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "--in-place", "-o", "b"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "--all-sheets", "-o", "b"}), CliError);
    EXPECT_THROW(parseCommandLine({"-i", "a.xlsx", "-s", "S", "-o", "b", "--dry-run=yes"}), CliError);
    EXPECT_THROW(parseCommandLine({"a.xlsx", "b.xlsx", "-s", "S", "--dry-run"}), CliError);
}
