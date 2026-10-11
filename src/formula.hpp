/**
 * @file formula.hpp
 * @brief Адреса ячеек и сдвиг относительных ссылок в формулах.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace redclean {

constexpr std::uint32_t kMaxRows = 1048576;    ///< Число строк листа Excel.
constexpr std::uint32_t kMaxColumns = 16384;   ///< Число столбцов листа Excel (XFD).

/// @brief Позиция ячейки, нумерация с 1.
struct CellPos
{
    std::uint32_t row = 0;    ///< Номер строки.
    std::uint32_t col = 0;    ///< Номер столбца.

    /// @brief Покомпонентное сравнение.
    friend bool operator==(const CellPos& a, const CellPos& b) { return a.row == b.row && a.col == b.col; }
};

/**
 * @brief Номер столбца по буквам.
 * @param letters Буквы столбца, регистр не важен.
 * @return @c "A" → 1, @c "XFD" → 16384; 0 при ошибке.
 */
std::uint32_t columnFromLetters(std::string_view letters);

/**
 * @brief Буквы столбца по номеру.
 * @param column Номер столбца, начиная с 1.
 * @return Например, 28 → @c "AB".
 */
std::string columnToLetters(std::uint32_t column);

/**
 * @brief Разбирает адрес ячейки.
 * @param ref Адрес вида @c "B12" или @c "$B$12".
 * @return Позиция или @c std::nullopt, если адрес некорректен или вне листа.
 */
std::optional<CellPos> parseCellRef(std::string_view ref);

/**
 * @brief Сдвигает относительные ссылки формулы так, как Excel делает при копировании.
 * @details Поддерживаются ссылки @c A1, @c A$1, @c $A1, диапазоны @c A1:B2, целые
 *          столбцы @c A:C и строки @c 1:3, в том числе с именем листа. Абсолютные части
 *          (@c $) не меняются; строки в кавычках, имена листов, функций и структурированные
 *          ссылки (@c Table1[Col]) не трогаются. Ссылка, ушедшая за пределы листа,
 *          заменяется на @c \#REF!.
 * @param formula Текст формулы без ведущего @c '='.
 * @param dRow Сдвиг по строкам.
 * @param dCol Сдвиг по столбцам.
 * @return Новый текст формулы.
 */
std::string shiftFormula(std::string_view formula, std::int64_t dRow, std::int64_t dCol);

}    // namespace redclean
