/**
 * @file test_formula.cpp
 * @brief Тесты адресов ячеек и сдвига формул.
 */

#include "formula.hpp"

#include <gtest/gtest.h>

using namespace redclean;

TEST(Formula, ColumnConversion)
{
    EXPECT_EQ(columnFromLetters("A"), 1u);
    EXPECT_EQ(columnFromLetters("z"), 26u);
    EXPECT_EQ(columnFromLetters("AA"), 27u);
    EXPECT_EQ(columnFromLetters("XFD"), 16384u);
    EXPECT_EQ(columnFromLetters("XFE"), 0u);
    EXPECT_EQ(columnFromLetters(""), 0u);
    EXPECT_EQ(columnToLetters(1), "A");
    EXPECT_EQ(columnToLetters(28), "AB");
    EXPECT_EQ(columnToLetters(16384), "XFD");
}

TEST(Formula, ParseCellRef)
{
    EXPECT_EQ(parseCellRef("B12"), (CellPos{12, 2}));
    EXPECT_EQ(parseCellRef("$AB$3"), (CellPos{3, 28}));
    EXPECT_FALSE(parseCellRef("B"));
    EXPECT_FALSE(parseCellRef("12"));
    EXPECT_FALSE(parseCellRef("B0"));
    EXPECT_FALSE(parseCellRef("A1048577"));
}

TEST(Formula, ShiftsRelativeReferences)
{
    EXPECT_EQ(shiftFormula("A1+B2", 1, 1), "B2+C3");
    EXPECT_EQ(shiftFormula("SUM(A1:B2)*2", 3, 0), "SUM(A4:B5)*2");
    EXPECT_EQ(shiftFormula("a1", 1, 0), "A2");
}

TEST(Formula, KeepsAbsoluteParts)
{
    EXPECT_EQ(shiftFormula("$A$1+A$1+$A1", 2, 3), "$A$1+D$1+$A3");
}

TEST(Formula, SheetQualifiedReferences)
{
    EXPECT_EQ(shiftFormula("Sheet1!A1+'Мой лист'!B$2", 1, 1), "Sheet1!B2+'Мой лист'!C$2");
    EXPECT_EQ(shiftFormula("'It''s A1'!A1", 1, 0), "'It''s A1'!A2");
}

TEST(Formula, LeavesNonReferencesAlone)
{
    EXPECT_EQ(shiftFormula("\"A1\"&A1", 1, 0), "\"A1\"&A2");
    EXPECT_EQ(shiftFormula("LOG10(A1)", 1, 0), "LOG10(A2)");
    EXPECT_EQ(shiftFormula("Table1[Col1]+A1", 1, 0), "Table1[Col1]+A2");
    EXPECT_EQ(shiftFormula("Table1[[#This Row],[B1]]", 1, 0), "Table1[[#This Row],[B1]]");
    EXPECT_EQ(shiftFormula("MyName*TRUE+1.5E+10", 5, 5), "MyName*TRUE+1.5E+10");
    EXPECT_EQ(shiftFormula("IF(A1>0,\"yes\",#N/A)", 1, 0), "IF(A2>0,\"yes\",#N/A)");
}

TEST(Formula, WholeColumnsAndRows)
{
    EXPECT_EQ(shiftFormula("SUM(A:B)", 5, 1), "SUM(B:C)");
    EXPECT_EQ(shiftFormula("SUM($A:B)", 5, 1), "SUM($A:C)");
    EXPECT_EQ(shiftFormula("SUM(1:2)", 3, 7), "SUM(4:5)");
}

TEST(Formula, OutOfRangeBecomesRefError)
{
    EXPECT_EQ(shiftFormula("A1", -1, 0), "#REF!");
    EXPECT_EQ(shiftFormula("XFD1", 0, 1), "#REF!");
    EXPECT_EQ(shiftFormula("A1048576", 1, 0), "#REF!");
}
