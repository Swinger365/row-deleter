/**
 * @file test_helpers.cpp
 * @brief Реализация вспомогательных средств тестов.
 */

#include "test_helpers.hpp"

#include "xml_utils.hpp"
#include "zip_archive.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <stdexcept>

namespace testutil {

namespace {

/// @brief XML-декларация частей пакета.
const char* const kXmlHeader = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";

/// @brief Экранирует строку для значения XML-атрибута в двойных кавычках.
std::string escapeAttr(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

}    // namespace

TempDir::TempDir()
{
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = fs::temp_directory_path() /
             ("red_cleaner_test_" + std::to_string(stamp) + "_" + std::to_string(counter++) + "_" +
              std::to_string(std::random_device{}()));
    fs::create_directories(m_path);
}

TempDir::~TempDir()
{
    std::error_code ec;
    fs::remove_all(m_path, ec);
}

std::string testStylesXml()
{
    return std::string(kXmlHeader) + R"(
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <fonts count="2">
    <font><sz val="11"/><color theme="1"/><name val="Calibri"/></font>
    <font><sz val="11"/><color rgb="FFFF0000"/><name val="Calibri"/></font>
  </fonts>
  <fills count="7">
    <fill><patternFill patternType="none"/></fill>
    <fill><patternFill patternType="gray125"/></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FFFF0000"/><bgColor indexed="64"/></patternFill></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FF00FF00"/><bgColor indexed="64"/></patternFill></fill>
    <fill><patternFill patternType="solid"><fgColor theme="5"/><bgColor indexed="64"/></patternFill></fill>
    <fill><patternFill patternType="solid"><fgColor indexed="10"/><bgColor indexed="64"/></patternFill></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FFC00000"/><bgColor indexed="64"/></patternFill></fill>
  </fills>
  <borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
  <cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
  <cellXfs count="7">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
    <xf numFmtId="0" fontId="0" fillId="2" borderId="0" xfId="0" applyFill="1"/>
    <xf numFmtId="0" fontId="0" fillId="3" borderId="0" xfId="0" applyFill="1"/>
    <xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/>
    <xf numFmtId="0" fontId="0" fillId="4" borderId="0" xfId="0" applyFill="1"/>
    <xf numFmtId="0" fontId="0" fillId="5" borderId="0" xfId="0" applyFill="1"/>
    <xf numFmtId="0" fontId="0" fillId="6" borderId="0" xfId="0" applyFill="1"/>
  </cellXfs>
  <cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>)";
}

std::string cell(const std::string& ref, unsigned style, const std::string& value, const std::string& type,
                 const std::string& formula)
{
    std::string out = "<c r=\"" + ref + "\"";
    if (style != 0) out += " s=\"" + std::to_string(style) + "\"";
    if (!type.empty()) out += " t=\"" + type + "\"";
    if (value.empty() && formula.empty()) return out + "/>";
    out += ">";
    if (!formula.empty()) out += "<f>" + formula + "</f>";
    if (!value.empty()) out += "<v>" + value + "</v>";
    return out + "</c>";
}

std::string row(unsigned number, const std::vector<std::string>& cells)
{
    std::string out = "<row r=\"" + std::to_string(number) + "\">";
    for (const auto& c : cells) out += c;
    return out + "</row>";
}

std::string worksheetXml(const std::string& sheetDataInner)
{
    return std::string(kXmlHeader) +
           R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
           R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
           "<sheetData>" + sheetDataInner + "</sheetData></worksheet>";
}

void writeWorkbook(const fs::path& path, const std::vector<FixtureSheet>& sheets, const FixtureOptions& options)
{
    redclean::ZipWriter zip;

    std::string types = std::string(kXmlHeader) +
                        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
                        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
                        R"(<Default Extension="xml" ContentType="application/xml"/>)"
                        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
                        R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)";
    std::string workbookRels = std::string(kXmlHeader) +
                               R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)";
    std::string sheetsXml;

    for (std::size_t i = 0; i < sheets.size(); ++i) {
        const std::string n = std::to_string(i + 1);
        types += R"(<Override PartName="/xl/worksheets/sheet)" + n +
                 R"(.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
        workbookRels += R"(<Relationship Id="rId)" + n +
                        R"(" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet)" +
                        n + R"(.xml"/>)";
        sheetsXml += "<sheet name=\"" + escapeAttr(sheets[i].name) + "\" sheetId=\"" + n + "\" r:id=\"rId" + n + "\"/>";
    }

    workbookRels += R"(<Relationship Id="rIdStyles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)";
    if (!options.themeXml.empty()) {
        types += R"(<Override PartName="/xl/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/>)";
        workbookRels += R"(<Relationship Id="rIdTheme" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="theme/theme1.xml"/>)";
    }
    if (options.withCalcChain) {
        types += R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/>)";
        workbookRels += R"(<Relationship Id="rIdCalc" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)";
    }
    types += "</Types>";
    workbookRels += "</Relationships>";

    zip.add("[Content_Types].xml", types);
    zip.add("_rels/.rels", std::string(kXmlHeader) +
                               R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                               R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                               "</Relationships>");
    zip.add("xl/workbook.xml", std::string(kXmlHeader) +
                                   R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
                                   R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                                   "<sheets>" + sheetsXml + "</sheets></workbook>");
    zip.add("xl/_rels/workbook.xml.rels", workbookRels);
    zip.add("xl/styles.xml", options.stylesXml);
    if (!options.themeXml.empty()) zip.add("xl/theme/theme1.xml", options.themeXml);
    if (options.withCalcChain)
        zip.add("xl/calcChain.xml", std::string(kXmlHeader) +
                                        R"(<calcChain xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><c r="A1" i="1"/></calcChain>)");
    for (std::size_t i = 0; i < sheets.size(); ++i)
        zip.add("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", worksheetXml(sheets[i].sheetDataInner));
    zip.finish(path.string());
}

std::string readPart(const fs::path& xlsx, const std::string& partPath)
{
    return redclean::ZipReader(xlsx.string()).read(partPath);
}

bool hasPart(const fs::path& xlsx, const std::string& partPath)
{
    return redclean::ZipReader(xlsx.string()).contains(partPath);
}

CellView findCell(const std::string& sheetXml, const std::string& ref)
{
    pugi::xml_document doc;
    redclean::xml::load(doc, sheetXml, "test");
    CellView view;
    const auto sheetData = redclean::xml::child(doc.document_element(), "sheetData");
    for (auto r = redclean::xml::child(sheetData, "row"); r; r = redclean::xml::nextSibling(r, "row")) {
        for (auto c = redclean::xml::child(r, "c"); c; c = redclean::xml::nextSibling(c, "c")) {
            if (ref != redclean::xml::attribute(c, "r").value()) continue;
            view.exists = true;
            if (auto s = redclean::xml::attribute(c, "s")) view.style = s.as_uint();
            view.type = redclean::xml::attribute(c, "t").value();
            if (auto v = redclean::xml::child(c, "v")) view.value = v.child_value();
            if (auto f = redclean::xml::child(c, "f")) {
                view.formula = f.child_value();
                view.formulaType = redclean::xml::attribute(f, "t").value();
                view.sharedIndex = redclean::xml::attribute(f, "si").value();
            }
            return view;
        }
    }
    return view;
}

}    // namespace testutil
