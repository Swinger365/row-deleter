/**
 * @file test_sheet_processor.cpp
 * @brief Тесты обработки одного листа.
 */

#include "sheet_processor.hpp"
#include "thread_pool.hpp"

#include "test_helpers.hpp"

#include <gtest/gtest.h>

using namespace redclean;
using namespace testutil;

namespace {

/// @brief Таблица красных стилей для testStylesXml() с параметрами по умолчанию.
RedStyleTable fillTable() { return RedStyleTable::build(testStylesXml(), "", StyleOptions{}); }

}    // namespace

/**
 * @test Очищаются строки с красной ячейкой (в том числе пустой) начиная с начальной строки;
 *       строки выше начальной, без красного и с зелёной заливкой не трогаются;
 *       форматирование ячеек сохраняется.
 */
TEST(SheetProcessor, ClearsRowsWithRedCellsFromStartRow)
{
    const std::string xml = worksheetXml(
        row(1, {cell("A1", kRedFill, "0", "s"), cell("B1", kDefault, "10")}) +
        row(2, {cell("A2", kDefault, "1", "s"), cell("B2", kDefault, "20")}) +
        row(3, {cell("A3", kDefault, "2", "s"), cell("B3", kRedFill, "30"), cell("C3", kGreenFill, "1", "b")}) +
        row(4, {cell("A4", kGreenFill, "40"), cell("B4", kDefault, "", "", "A4*2")}) +
        row(5, {cell("A5", kDefault, "50"), cell("D5", kDarkRedFill)}));

    const SheetResult result = processSheetXml(xml, 2, fillTable());
    ASSERT_TRUE(result.modified);
    EXPECT_EQ(result.stats.rowsScanned, 4u);
    EXPECT_EQ(result.stats.redRows, (std::vector<std::uint32_t>{3, 5}));
    EXPECT_EQ(result.stats.cellsCleared, 4u);

    EXPECT_EQ(findCell(result.xml, "A1").value, "0");
    EXPECT_EQ(findCell(result.xml, "A1").type, "s");
    EXPECT_EQ(findCell(result.xml, "B1").value, "10");

    EXPECT_EQ(findCell(result.xml, "B2").value, "20");

    for (const char* ref : {"A3", "B3", "C3", "A5", "D5"}) {
        const CellView c = findCell(result.xml, ref);
        EXPECT_TRUE(c.exists) << ref;
        EXPECT_FALSE(c.value) << ref;
        EXPECT_FALSE(c.formula) << ref;
        EXPECT_TRUE(c.type.empty()) << ref;
    }
    EXPECT_EQ(findCell(result.xml, "B3").style, kRedFill);
    EXPECT_EQ(findCell(result.xml, "C3").style, kGreenFill);
    EXPECT_FALSE(findCell(result.xml, "A3").style);

    EXPECT_EQ(findCell(result.xml, "A4").value, "40");
    EXPECT_EQ(findCell(result.xml, "B4").formula, "A4*2");
}

TEST(SheetProcessor, NoRedCellsLeavesXmlUntouched)
{
    const std::string xml = worksheetXml(row(1, {cell("A1", kGreenFill, "1")}) + row(2, {cell("A2", kRedFont, "2")}));
    const SheetResult result = processSheetXml(xml, 1, fillTable());
    EXPECT_FALSE(result.modified);
    EXPECT_EQ(result.xml, xml);
    EXPECT_TRUE(result.stats.redRows.empty());
}

TEST(SheetProcessor, StartRowBeyondDataDoesNothing)
{
    const std::string xml = worksheetXml(row(1, {cell("A1", kRedFill, "1")}));
    const SheetResult result = processSheetXml(xml, 100, fillTable());
    EXPECT_FALSE(result.modified);
    EXPECT_EQ(result.stats.rowsScanned, 0u);
}

TEST(SheetProcessor, RowsWithoutNumbersAreCountedSequentially)
{
    const std::string xml = worksheetXml("<row><c s=\"1\"><v>1</v></c></row>"
                                         "<row><c s=\"1\"><v>2</v></c></row>"
                                         "<row r=\"10\"><c r=\"A10\" s=\"1\"><v>3</v></c></row>"
                                         "<row><c s=\"1\"><v>4</v></c></row>");
    const SheetResult result = processSheetXml(xml, 2, fillTable());
    EXPECT_EQ(result.stats.redRows, (std::vector<std::uint32_t>{2, 10, 11}));
}

TEST(SheetProcessor, InlineStringsAndAttributesAreRemoved)
{
    const std::string xml = worksheetXml(
        R"(<row r="1"><c r="A1" s="1" t="inlineStr" cm="1" vm="2"><is><t xml:space="preserve"> text </t></is></c></row>)");
    const SheetResult result = processSheetXml(xml, 1, fillTable());
    ASSERT_TRUE(result.modified);
    EXPECT_NE(result.xml.find(R"(<c r="A1" s="1"/>)"), std::string::npos) << result.xml;
}

TEST(SheetProcessor, KeepsWhitespaceInUntouchedCells)
{
    const std::string xml = worksheetXml(
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t xml:space="preserve">  </t></is></c></row>)"
        R"(<row r="2"><c r="A2" s="1"><v>1</v></c></row>)");
    const SheetResult result = processSheetXml(xml, 1, fillTable());
    ASSERT_TRUE(result.modified);
    EXPECT_NE(result.xml.find(R"(<t xml:space="preserve">  </t>)"), std::string::npos) << result.xml;
}

/**
 * @test Если очищена главная ячейка общей формулы, зависимые получают явные формулы;
 *       другая общая формула не затрагивается.
 */
TEST(SheetProcessor, SharedFormulaMasterInClearedRowIsExpanded)
{
    const std::string xml = worksheetXml(
        R"(<row r="2"><c r="A2" s="1"><v>1</v></c><c r="B2"><f t="shared" ref="B2:B5" si="0">A2*$C$1+SUM(A$1:A2)</f><v>1</v></c></row>)"
        R"(<row r="3"><c r="A3"><v>2</v></c><c r="B3"><f t="shared" si="0"/><v>2</v></c></row>)"
        R"(<row r="4"><c r="A4" s="1"><v>3</v></c><c r="B4"><f t="shared" si="0"/><v>3</v></c></row>)"
        R"(<row r="5"><c r="A5"><v>4</v></c><c r="B5"><f t="shared" si="0"/><v>4</v></c></row>)"
        R"(<row r="6"><c r="B6"><f t="shared" ref="B6:B7" si="1">A6+1</f><v>1</v></c></row>)"
        R"(<row r="7"><c r="B7"><f t="shared" si="1"/><v>1</v></c></row>)");

    const SheetResult result = processSheetXml(xml, 1, fillTable());
    ASSERT_TRUE(result.modified);
    EXPECT_EQ(result.stats.redRows, (std::vector<std::uint32_t>{2, 4}));
    EXPECT_EQ(result.stats.formulasRemoved, 2u);
    EXPECT_EQ(result.stats.sharedFormulasExpanded, 2u);

    EXPECT_FALSE(findCell(result.xml, "B2").formula);
    EXPECT_FALSE(findCell(result.xml, "B4").formula);

    const CellView b3 = findCell(result.xml, "B3");
    EXPECT_EQ(b3.formula, "A3*$C$1+SUM(A$1:A3)");
    EXPECT_TRUE(b3.formulaType.empty());
    EXPECT_TRUE(b3.sharedIndex.empty());
    EXPECT_EQ(b3.value, "2");
    EXPECT_EQ(findCell(result.xml, "B5").formula, "A5*$C$1+SUM(A$1:A5)");

    EXPECT_EQ(findCell(result.xml, "B6").formulaType, "shared");
    EXPECT_EQ(findCell(result.xml, "B7").sharedIndex, "1");
}

TEST(SheetProcessor, PrefixedNamespaceIsSupported)
{
    const std::string xml =
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><x:sheetData>)"
        R"(<x:row r="1"><x:c r="A1" s="1"><x:v>1</x:v></x:c></x:row>)"
        R"(<x:row r="2"><x:c r="A2"><x:v>2</x:v></x:c></x:row>)"
        R"(</x:sheetData></x:worksheet>)";
    const SheetResult result = processSheetXml(xml, 1, fillTable());
    EXPECT_EQ(result.stats.redRows, (std::vector<std::uint32_t>{1}));
    EXPECT_NE(result.xml.find(R"(<x:c r="A1" s="1"/>)"), std::string::npos) << result.xml;
    EXPECT_NE(result.xml.find("<x:v>2</x:v>"), std::string::npos);
}

TEST(SheetProcessor, ParallelDetectionMatchesSequential)
{
    std::string data;
    std::vector<std::uint32_t> expected;
    for (unsigned r = 1; r <= 5000; ++r) {
        const bool red = (r * 7919u) % 13u == 0;
        if (red && r >= 100) expected.push_back(r);
        data += row(r, {cell("A" + std::to_string(r), kDefault, std::to_string(r)),
                        cell("C" + std::to_string(r), red ? kRedFill : kGreenFill, "1")});
    }
    const std::string xml = worksheetXml(data);
    const RedStyleTable table = fillTable();

    SheetProcessor processor("big", 100);
    processor.load(xml);
    ThreadPool pool(4);
    parallelFor(pool, processor.candidateRowCount(), 37,
                [&](std::size_t begin, std::size_t end) { processor.detect(begin, end, table); });
    processor.apply();

    EXPECT_EQ(processor.stats().redRows, expected);
    EXPECT_EQ(processor.serialize(), processSheetXml(xml, 100, table).xml);
}

TEST(SheetProcessor, MalformedXmlThrows)
{
    SheetProcessor processor("bad", 1);
    EXPECT_THROW(processor.load("<worksheet><sheetData>"), std::runtime_error);
}

TEST(SheetProcessor, ZeroStartRowIsRejected) { EXPECT_THROW(SheetProcessor("x", 0), std::invalid_argument); }
