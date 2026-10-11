/**
 * @file test_cleaner.cpp
 * @brief Тесты cleanWorkbook на настоящих .xlsx-файлах.
 */

#include "cleaner.hpp"

#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using namespace redclean;
using namespace testutil;

namespace {

/// @brief Два листа с красными строками, формулой и строками выше начальной.
std::vector<FixtureSheet> sampleSheets()
{
    return {
        {"Лист1", row(1, {cell("A1", kRedFill, "header")}) +
                      row(2, {cell("A2", kDefault, "1"), cell("B2", kRedFill, "2")}) +
                      row(3, {cell("A3", kDefault, "3"), cell("B3", kDefault, "", "", "A3+1")}) +
                      row(4, {cell("A4", kIndexedRed, "4"), cell("B4", kDefault, "", "", "A4+1")})},
        {"Данные", row(1, {cell("A1", kDefault, "x")}) + row(2, {cell("A2", kRedFill, "y")}) +
                       row(3, {cell("C3", kDarkRedFill, "z")})},
        {"Нетронутый", row(1, {cell("A1", kRedFill, "keep")})},
    };
}

/// @brief Параметры с входным и выходным файлами в @p dir.
CleanOptions baseOptions(const TempDir& dir)
{
    CleanOptions options;
    options.inputPath = dir.file("in.xlsx").string();
    options.outputPath = dir.file("out.xlsx").string();
    options.threads = 4;
    return options;
}

}    // namespace

/**
 * @test Обрабатываются только выбранные листы, каждый со своей начальной строки;
 *       строки выше начальной, невыбранный лист и прочие части копируются как есть;
 *       после удаления формулы цепочка вычислений корректно сбрасывается.
 */
TEST(Cleaner, ProcessesOnlySelectedSheetsFromTheirStartRows)
{
    TempDir dir;
    FixtureOptions fixture;
    fixture.withCalcChain = true;
    writeWorkbook(dir.file("in.xlsx"), sampleSheets(), fixture);

    CleanOptions options = baseOptions(dir);
    options.defaultStartRow = 2;
    options.sheets = {{"Лист1", std::nullopt}, {"Данные", 3u}};
    const CleanReport report = cleanWorkbook(options);

    ASSERT_TRUE(report.written);
    ASSERT_EQ(report.sheets.size(), 2u);
    EXPECT_EQ(report.redStyleCount, 3u);
    EXPECT_EQ(report.threadsUsed, 4u);
    EXPECT_EQ(report.sheets[0].stats.redRows, (std::vector<std::uint32_t>{2, 4}));
    EXPECT_EQ(report.sheets[1].startRow, 3u);
    EXPECT_EQ(report.sheets[1].stats.redRows, (std::vector<std::uint32_t>{3}));

    const auto out = dir.file("out.xlsx");
    const std::string sheet1 = readPart(out, "xl/worksheets/sheet1.xml");
    EXPECT_EQ(findCell(sheet1, "A1").value, "header");
    EXPECT_FALSE(findCell(sheet1, "A2").value);
    EXPECT_FALSE(findCell(sheet1, "B2").value);
    EXPECT_EQ(findCell(sheet1, "B2").style, kRedFill);
    EXPECT_EQ(findCell(sheet1, "B3").formula, "A3+1");
    EXPECT_FALSE(findCell(sheet1, "A4").value);
    EXPECT_FALSE(findCell(sheet1, "B4").formula);

    const std::string sheet2 = readPart(out, "xl/worksheets/sheet2.xml");
    EXPECT_EQ(findCell(sheet2, "A2").value, "y");
    EXPECT_FALSE(findCell(sheet2, "C3").value);

    const auto in = dir.file("in.xlsx");
    EXPECT_EQ(readPart(out, "xl/worksheets/sheet3.xml"), readPart(in, "xl/worksheets/sheet3.xml"));
    EXPECT_EQ(readPart(out, "xl/styles.xml"), readPart(in, "xl/styles.xml"));
    EXPECT_EQ(readPart(out, "xl/workbook.xml"), readPart(in, "xl/workbook.xml"));

    EXPECT_TRUE(report.calcChainRemoved);
    EXPECT_FALSE(hasPart(out, "xl/calcChain.xml"));
    EXPECT_EQ(readPart(out, "xl/_rels/workbook.xml.rels").find("calcChain"), std::string::npos);
    EXPECT_EQ(readPart(out, "[Content_Types].xml").find("calcChain"), std::string::npos);
}

TEST(Cleaner, KeepsCalcChainWhenNoFormulaRemoved)
{
    TempDir dir;
    FixtureOptions fixture;
    fixture.withCalcChain = true;
    writeWorkbook(dir.file("in.xlsx"), sampleSheets(), fixture);

    CleanOptions options = baseOptions(dir);
    options.sheets = {{"Данные", std::nullopt}};
    const CleanReport report = cleanWorkbook(options);
    EXPECT_FALSE(report.calcChainRemoved);
    EXPECT_TRUE(hasPart(dir.file("out.xlsx"), "xl/calcChain.xml"));
}

TEST(Cleaner, AllSheets)
{
    TempDir dir;
    writeWorkbook(dir.file("in.xlsx"), sampleSheets());
    CleanOptions options = baseOptions(dir);
    options.allSheets = true;
    const CleanReport report = cleanWorkbook(options);
    ASSERT_EQ(report.sheets.size(), 3u);
    EXPECT_EQ(report.sheets[2].stats.redRows, (std::vector<std::uint32_t>{1}));
    EXPECT_FALSE(findCell(readPart(dir.file("out.xlsx"), "xl/worksheets/sheet3.xml"), "A1").value);
}

TEST(Cleaner, DryRunDoesNotWrite)
{
    TempDir dir;
    writeWorkbook(dir.file("in.xlsx"), sampleSheets());
    CleanOptions options = baseOptions(dir);
    options.dryRun = true;
    options.outputPath.clear();
    options.sheets = {{"Лист1", std::nullopt}};
    const CleanReport report = cleanWorkbook(options);
    EXPECT_FALSE(report.written);
    EXPECT_EQ(report.sheets[0].stats.redRows, (std::vector<std::uint32_t>{1, 2, 4}));
    EXPECT_FALSE(fs::exists(dir.file("out.xlsx")));
    EXPECT_EQ(findCell(readPart(dir.file("in.xlsx"), "xl/worksheets/sheet1.xml"), "A1").value, "header");
}

TEST(Cleaner, InPlace)
{
    TempDir dir;
    writeWorkbook(dir.file("in.xlsx"), sampleSheets());
    CleanOptions options = baseOptions(dir);
    options.outputPath = options.inputPath;
    options.sheets = {{"Данные", std::nullopt}};
    ASSERT_TRUE(cleanWorkbook(options).written);
    const std::string sheet2 = readPart(dir.file("in.xlsx"), "xl/worksheets/sheet2.xml");
    EXPECT_EQ(findCell(sheet2, "A1").value, "x");
    EXPECT_FALSE(findCell(sheet2, "A2").value);
}

TEST(Cleaner, FontTarget)
{
    TempDir dir;
    writeWorkbook(dir.file("in.xlsx"),
                  {{"S", row(1, {cell("A1", kRedFont, "1")}) + row(2, {cell("A2", kRedFill, "2")})}});
    CleanOptions options = baseOptions(dir);
    options.sheets = {{"S", std::nullopt}};
    options.style.target = ColorTarget::Font;
    const CleanReport report = cleanWorkbook(options);
    EXPECT_EQ(report.sheets[0].stats.redRows, (std::vector<std::uint32_t>{1}));
}

/**
 * @test Ошибки параметров; сообщение о ненайденном листе содержит список доступных листов.
 */
TEST(Cleaner, Errors)
{
    TempDir dir;
    writeWorkbook(dir.file("in.xlsx"), sampleSheets());

    CleanOptions options = baseOptions(dir);
    options.sheets = {{"Нет такого", std::nullopt}};
    try {
        cleanWorkbook(options);
        FAIL() << "ожидалось исключение";
    }
    catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("Лист1"), std::string::npos);
    }

    options.sheets = {{"Лист1", std::nullopt}, {"Лист1", 5u}};
    EXPECT_THROW(cleanWorkbook(options), std::invalid_argument);

    options.sheets.clear();
    EXPECT_THROW(cleanWorkbook(options), std::invalid_argument);

    options.sheets = {{"Лист1", std::nullopt}};
    options.inputPath = dir.file("missing.xlsx").string();
    EXPECT_THROW(cleanWorkbook(options), std::runtime_error);

    std::ofstream(dir.file("junk.xlsx")) << "not a zip";
    options.inputPath = dir.file("junk.xlsx").string();
    EXPECT_THROW(cleanWorkbook(options), std::runtime_error);
}

/**
 * @test Большой случайный лист: результат не зависит от числа потоков и размера задач
 *       и совпадает с ожидаемым.
 */
TEST(Cleaner, LargeWorkbookIsDeterministicAcrossThreadCounts)
{
    TempDir dir;
    std::mt19937 rng(12345);
    std::vector<FixtureSheet> sheets;
    std::vector<std::vector<std::uint32_t>> expected;
    const std::uint32_t startRow = 50;
    for (int s = 0; s < 3; ++s) {
        std::string data;
        std::vector<std::uint32_t> red;
        for (std::uint32_t r = 1; r <= 20000; ++r) {
            std::vector<std::string> cells;
            bool hasRed = false;
            for (const char* col : {"A", "B", "C", "D"}) {
                const unsigned roll = rng() % 100;
                const unsigned style = roll < 2 ? kRedFill : roll < 3 ? kDarkRedFill : roll < 20 ? kGreenFill : kDefault;
                hasRed = hasRed || style == kRedFill || style == kDarkRedFill;
                cells.push_back(cell(col + std::to_string(r), style, std::to_string(rng() % 1000)));
            }
            if (hasRed && r >= startRow) red.push_back(r);
            data += row(r, cells);
        }
        sheets.push_back({"S" + std::to_string(s), data});
        expected.push_back(red);
    }
    writeWorkbook(dir.file("in.xlsx"), sheets);

    std::string reference;
    for (const auto& [threads, grain] : std::vector<std::pair<std::size_t, std::size_t>>{{1, 100000}, {4, 333}, {8, 7}}) {
        CleanOptions options = baseOptions(dir);
        options.outputPath = dir.file("out_" + std::to_string(threads) + ".xlsx").string();
        options.threads = threads;
        options.rowsPerTask = grain;
        options.defaultStartRow = startRow;
        options.allSheets = true;
        const CleanReport report = cleanWorkbook(options);
        for (std::size_t s = 0; s < 3; ++s) EXPECT_EQ(report.sheets[s].stats.redRows, expected[s]) << "threads=" << threads;

        std::string combined;
        for (int s = 1; s <= 3; ++s) combined += readPart(options.outputPath, "xl/worksheets/sheet" + std::to_string(s) + ".xml");
        if (reference.empty())
            reference = combined;
        else
            EXPECT_EQ(combined, reference) << "threads=" << threads;
    }
}
