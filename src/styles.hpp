/**
 * @file styles.hpp
 * @brief Анализ @c xl/styles.xml: какие стили ячеек считаются красными.
 */

#pragma once

#include "color.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace redclean {

/// @brief Какой цвет ячейки проверять.
enum class ColorTarget {
    Fill,    ///< Цвет заливки.
    Font,    ///< Цвет шрифта.
    Any      ///< Заливка или шрифт.
};

/// @brief Параметры анализа стилей.
struct StyleOptions
{
    RedCriteria criteria;                     ///< Что считать красным.
    ColorTarget target = ColorTarget::Fill;   ///< Что проверять у ячейки.
};

/**
 * @brief 12 цветов темы в порядке индексов атрибута @c theme="N" в styles.xml.
 * @details 0 — lt1, 1 — dk1, 2 — lt2, 3 — dk2, 4..9 — accent1..6, 10 — hlink, 11 — folHlink.
 *          Первые четыре идут не в том порядке, что в theme1.xml, — так устроен Excel.
 */
using ThemePalette = std::array<Rgb, 12>;

/**
 * @brief Палитра темы Office 2013+ по умолчанию.
 * @return Палитра.
 */
ThemePalette defaultThemePalette();

/**
 * @brief Разбирает палитру из @c xl/theme/themeN.xml.
 * @param themeXml Текст части темы; пустая строка — тема по умолчанию.
 * @return Палитра; отсутствующие цвета берутся из темы по умолчанию.
 * @throws std::runtime_error Если XML некорректен.
 */
ThemePalette parseThemePalette(const std::string& themeXml);

/**
 * @brief Таблица «индекс стиля ячейки (атрибут @c s) → красный или нет».
 * @details Строится один раз из styles.xml, после чего только читается,
 *          поэтому безопасна для одновременного доступа из нескольких потоков.
 */
class RedStyleTable
{
public:
    /// @brief Пустая таблица: ни один стиль не красный.
    RedStyleTable() = default;

    /**
     * @brief Таблица из готовых флагов.
     * @param redFlags Флаг для каждого индекса @c cellXfs.
     */
    explicit RedStyleTable(std::vector<bool> redFlags);

    /**
     * @brief Строит таблицу по частям книги.
     * @details Учитываются цвета @c rgb, @c theme (с @c tint) и @c indexed,
     *          в том числе пользовательская палитра @c &lt;indexedColors&gt;.
     *          Для узорной заливки проверяются цвет узора и фона, для градиентной — все опорные точки.
     * @param stylesXml Текст @c xl/styles.xml; пустой — все стили не красные.
     * @param themeXml Текст части темы; может быть пустым.
     * @param options Параметры анализа.
     * @return Таблица стилей.
     * @throws std::runtime_error Если XML некорректен.
     */
    static RedStyleTable build(const std::string& stylesXml, const std::string& themeXml, const StyleOptions& options);

    /**
     * @brief Проверяет стиль.
     * @param styleIndex Значение атрибута @c s ячейки (0, если атрибута нет).
     * @return @c true, если стиль красный; для несуществующих индексов — @c false.
     */
    bool isRed(std::uint32_t styleIndex) const { return styleIndex < m_red.size() && m_red[styleIndex]; }

    /// @brief Число стилей ячеек в книге.
    std::size_t styleCount() const { return m_red.size(); }

    /// @brief Число красных стилей.
    std::size_t redCount() const;

private:
    std::vector<bool> m_red;
};

}    // namespace redclean
