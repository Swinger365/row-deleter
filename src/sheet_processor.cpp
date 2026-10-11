/**
 * @file sheet_processor.cpp
 * @brief Реализация обработки листа.
 */

#include "sheet_processor.hpp"

#include "formula.hpp"
#include "xml_utils.hpp"

#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace redclean {

namespace {

/**
 * @brief Обходит ячейки строки, вычисляя номер столбца.
 * @details Атрибут @c r у ячейки необязателен; без него столбец — предыдущий + 1.
 * @tparam Fn Вызываемый объект @c void(pugi::xml_node cell, std::uint32_t col).
 * @param row Элемент @c &lt;row&gt;.
 * @param fn Обработчик.
 */
template <typename Fn>
void forEachCell(const pugi::xml_node& row, Fn&& fn)
{
    std::uint32_t col = 0;
    xml::forEachChild(row, "c", [&](const pugi::xml_node& cell) {
        const auto pos = parseCellRef(xml::attribute(cell, "r").value());
        col = pos ? pos->col : col + 1;
        fn(cell, col);
    });
}

/// @brief Является ли элемент @c &lt;f&gt; частью общей формулы.
bool isSharedFormula(const pugi::xml_node& f) { return f && std::string_view(xml::attribute(f, "t").value()) == "shared"; }

/**
 * @brief Очищает ячейку: удаляет значение, формулу, тип и метаданные, оставляя адрес (@c r) и стиль (@c s).
 * @param cell Элемент @c &lt;c&gt;.
 * @param[out] touched Устанавливается в @c true, если что-то было удалено.
 * @return @c true, если в ячейке было содержимое.
 */
bool clearCell(pugi::xml_node cell, bool& touched)
{
    bool hadContent = false;
    for (pugi::xml_node node = cell.first_child(); node;) {
        const pugi::xml_node next = node.next_sibling();
        if (node.type() == pugi::node_element) hadContent = true;
        cell.remove_child(node);
        touched = true;
        node = next;
    }
    for (pugi::xml_attribute attr = cell.first_attribute(); attr;) {
        const pugi::xml_attribute next = attr.next_attribute();
        const std::string_view name = attr.name();
        if (name != "r" && name != "s") {
            cell.remove_attribute(attr);
            touched = true;
        }
        attr = next;
    }
    return hadContent;
}

/// @brief Мастер-ячейка общей формулы.
struct SharedMaster
{
    std::string formula;    ///< Текст формулы мастера.
    CellPos pos;            ///< Позиция мастера.
};

}    // namespace

SheetProcessor::SheetProcessor(std::string name, std::uint32_t startRow) : m_name(std::move(name)), m_startRow(startRow)
{
    if (m_startRow == 0) throw std::invalid_argument("Номер начальной строки должен быть >= 1");
}

void SheetProcessor::load(const std::string& xml)
{
    xml::load(m_doc, xml, "лист \"" + m_name + "\"");
    m_sheetData = xml::child(m_doc.document_element(), "sheetData");
    m_rows.clear();

    std::uint32_t number = 0;
    xml::forEachChild(m_sheetData, "row", [&](const pugi::xml_node& row) {
        const unsigned r = xml::attribute(row, "r").as_uint(0);
        number = r != 0 ? r : number + 1;
        if (number >= m_startRow) m_rows.push_back({row, number});
    });
    m_redFlags.assign(m_rows.size(), 0);
    m_stats = SheetStats{};
    m_stats.rowsScanned = m_rows.size();
    m_modified = false;
}

void SheetProcessor::detect(std::size_t begin, std::size_t end, const RedStyleTable& styles)
{
    for (std::size_t i = begin; i < end && i < m_rows.size(); ++i) {
        for (pugi::xml_node cell = xml::child(m_rows[i].node, "c"); cell; cell = xml::nextSibling(cell, "c")) {
            if (styles.isRed(xml::attribute(cell, "s").as_uint(0))) {
                m_redFlags[i] = 1;
                break;
            }
        }
    }
}

void SheetProcessor::apply()
{
    std::unordered_map<std::string, SharedMaster> clearedMasters;

    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (!m_redFlags[i]) continue;
        const RowRef& row = m_rows[i];
        m_stats.redRows.push_back(row.number);

        forEachCell(row.node, [&](pugi::xml_node cell, std::uint32_t col) {
            const pugi::xml_node f = xml::child(cell, "f");
            if (f) {
                ++m_stats.formulasRemoved;
                if (isSharedFormula(f) && xml::attribute(f, "ref") && *f.child_value())
                    clearedMasters[xml::attribute(f, "si").value()] = {f.child_value(), {row.number, col}};
            }
            if (clearCell(cell, m_modified)) ++m_stats.cellsCleared;
        });
    }

    if (clearedMasters.empty()) return;

    std::uint32_t number = 0;
    xml::forEachChild(m_sheetData, "row", [&](const pugi::xml_node& row) {
        const unsigned r = xml::attribute(row, "r").as_uint(0);
        number = r != 0 ? r : number + 1;
        forEachCell(row, [&](pugi::xml_node cell, std::uint32_t col) {
            pugi::xml_node f = xml::child(cell, "f");
            if (!isSharedFormula(f)) return;
            const auto it = clearedMasters.find(xml::attribute(f, "si").value());
            if (it == clearedMasters.end()) return;

            const SharedMaster& master = it->second;
            const std::string formula =
                shiftFormula(master.formula, static_cast<std::int64_t>(number) - master.pos.row,
                             static_cast<std::int64_t>(col) - master.pos.col);
            for (const char* attrName : {"t", "si", "ref"}) f.remove_attribute(attrName);
            f.text().set(formula.c_str());
            ++m_stats.sharedFormulasExpanded;
            m_modified = true;
        });
    });
}

std::string SheetProcessor::serialize() const { return xml::serialize(m_doc); }

SheetResult processSheetXml(const std::string& xml, std::uint32_t startRow, const RedStyleTable& styles)
{
    SheetProcessor processor("sheet", startRow);
    processor.load(xml);
    processor.detect(0, processor.candidateRowCount(), styles);
    processor.apply();
    return {processor.modified() ? processor.serialize() : xml, processor.modified(), processor.stats()};
}

}    // namespace redclean
