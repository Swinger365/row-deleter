/**
 * @file test_helpers.hpp
 * @brief Вспомогательные средства тестов: временные папки и генерация .xlsx.
 */

#pragma once

#include <pugixml.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/// @brief Вспомогательные средства тестов.
namespace testutil {

namespace fs = std::filesystem;

/// @brief Временная папка, удаляемая вместе с содержимым в деструкторе.
class TempDir
{
public:
    /// @brief Создаёт папку с уникальным именем во временном каталоге системы.
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// @brief Путь к файлу @p name внутри папки.
    fs::path file(const std::string& name) const { return m_path / name; }

private:
    fs::path m_path;
};

/// @brief Индексы стилей (cellXfs) в testStylesXml().
enum Style : unsigned {
    kDefault = 0,         ///< Без заливки.
    kRedFill = 1,         ///< Заливка FFFF0000.
    kGreenFill = 2,       ///< Заливка FF00FF00.
    kRedFont = 3,         ///< Красный шрифт без заливки.
    kThemeOrange = 4,     ///< Заливка theme=5 (accent2, в теме по умолчанию оранжевый).
    kIndexedRed = 5,      ///< Заливка indexed=10.
    kDarkRedFill = 6,     ///< Заливка FFC00000.
    kStyleCount = 7,      ///< Число стилей.
};

/// @brief styles.xml со стилями из перечисления Style.
std::string testStylesXml();

/**
 * @brief XML ячейки @c &lt;c&gt;.
 * @param ref Адрес, например @c "B3".
 * @param style Индекс стиля (атрибут @c s); 0 — атрибут не пишется.
 * @param value Содержимое @c &lt;v&gt;; пусто — элемента нет.
 * @param type Атрибут @c t; пусто — атрибута нет.
 * @param formula Содержимое @c &lt;f&gt;; пусто — элемента нет.
 * @return Текст XML.
 */
std::string cell(const std::string& ref, unsigned style, const std::string& value = "", const std::string& type = "",
                 const std::string& formula = "");

/**
 * @brief XML строки @c &lt;row&gt;.
 * @param number Номер строки (атрибут @c r).
 * @param cells XML ячеек.
 * @return Текст XML.
 */
std::string row(unsigned number, const std::vector<std::string>& cells);

/**
 * @brief XML листа целиком.
 * @param sheetDataInner Содержимое @c &lt;sheetData&gt;.
 * @return Текст XML.
 */
std::string worksheetXml(const std::string& sheetDataInner);

/// @brief Лист тестовой книги.
struct FixtureSheet
{
    std::string name;              ///< Имя листа.
    std::string sheetDataInner;    ///< Содержимое @c &lt;sheetData&gt;.
};

/// @brief Параметры тестовой книги.
struct FixtureOptions
{
    std::string stylesXml = testStylesXml();    ///< Часть стилей.
    std::string themeXml;                       ///< Часть темы; пусто — без темы.
    bool withCalcChain = false;                 ///< Добавить xl/calcChain.xml.
};

/**
 * @brief Создаёт настоящий .xlsx.
 * @param path Путь к файлу.
 * @param sheets Листы; сохраняются как xl/worksheets/sheet1.xml, sheet2.xml, ... в заданном порядке.
 * @param options Параметры книги.
 */
void writeWorkbook(const fs::path& path, const std::vector<FixtureSheet>& sheets, const FixtureOptions& options = {});

/**
 * @brief Читает часть из .xlsx.
 * @param xlsx Путь к книге.
 * @param partPath Путь части в архиве.
 * @return Содержимое.
 * @throws std::runtime_error Если части нет.
 */
std::string readPart(const fs::path& xlsx, const std::string& partPath);

/// @brief Есть ли часть @p partPath в книге @p xlsx.
bool hasPart(const fs::path& xlsx, const std::string& partPath);

/// @brief Разобранная ячейка листа.
struct CellView
{
    bool exists = false;                    ///< Ячейка найдена.
    std::optional<unsigned> style;          ///< Атрибут @c s.
    std::string type;                       ///< Атрибут @c t.
    std::optional<std::string> value;       ///< Содержимое @c &lt;v&gt;.
    std::optional<std::string> formula;     ///< Содержимое @c &lt;f&gt;.
    std::string formulaType;                ///< Атрибут @c t у @c &lt;f&gt;.
    std::string sharedIndex;                ///< Атрибут @c si у @c &lt;f&gt;.
};

/**
 * @brief Находит ячейку в XML листа.
 * @param sheetXml XML листа.
 * @param ref Адрес ячейки.
 * @return Описание ячейки; CellView::exists равно @c false, если её нет.
 */
CellView findCell(const std::string& sheetXml, const std::string& ref);

}    // namespace testutil
