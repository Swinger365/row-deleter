/**
 * @file sheet_processor.hpp
 * @brief Поиск и очистка строк с красными ячейками на одном листе.
 */

#pragma once

#include "styles.hpp"

#include <pugixml.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace redclean {

/// @brief Статистика обработки листа.
struct SheetStats
{
    std::size_t rowsScanned = 0;               ///< Строк с номером не меньше начального, присутствующих в XML.
    std::size_t cellsCleared = 0;              ///< Очищенных ячеек, в которых было содержимое.
    std::size_t formulasRemoved = 0;           ///< Удалённых формул.
    std::size_t sharedFormulasExpanded = 0;    ///< Ячеек общей формулы, получивших собственную формулу.
    std::vector<std::uint32_t> redRows;        ///< Номера строк с красными ячейками, по возрастанию.
};

/**
 * @brief Обработка одного листа (@c xl/worksheets/sheetN.xml) в три этапа.
 * @details
 *  1. @ref load — разбор XML; разные листы можно разбирать параллельно.
 *  2. @ref detect — поиск красных строк; можно вызывать параллельно для
 *     непересекающихся диапазонов, документ при этом только читается.
 *  3. @ref apply — очистка найденных строк; pugixml не допускает одновременных
 *     изменений одного документа, поэтому этап выполняется одним потоком на лист.
 *
 * Очистка удаляет значения, формулы и тип ячейки, оставляя адрес и стиль, — как
 * команда «Очистить содержимое» в Excel.
 */
class SheetProcessor
{
public:
    /**
     * @brief Создаёт обработчик.
     * @param name Имя листа (для сообщений и отчёта).
     * @param startRow Номер строки, начиная с которой ищутся красные ячейки.
     * @throws std::invalid_argument Если @p startRow равен 0.
     */
    SheetProcessor(std::string name, std::uint32_t startRow);

    /**
     * @brief Разбирает XML листа и собирает строки-кандидаты.
     * @details Номер строки берётся из атрибута @c r, а при его отсутствии — как предыдущий + 1.
     * @param xml Текст части листа.
     * @throws std::runtime_error Если XML некорректен.
     */
    void load(const std::string& xml);

    /// @brief Число строк-кандидатов (номер не меньше начального).
    std::size_t candidateRowCount() const { return m_rows.size(); }

    /**
     * @brief Отмечает кандидатов из диапазона [@p begin, @p end), содержащих красную ячейку.
     * @details Безопасен для одновременного вызова из разных потоков с непересекающимися диапазонами.
     * @param begin Первый индекс кандидата.
     * @param end Индекс после последнего; ограничивается @ref candidateRowCount().
     * @param styles Таблица красных стилей.
     */
    void detect(std::size_t begin, std::size_t end, const RedStyleTable& styles);

    /**
     * @brief Очищает отмеченные строки.
     * @details Если очищается мастер-ячейка общей формулы (@c t="shared"), зависимые ячейки
     *          получают собственные формулы со сдвинутыми ссылками, иначе Excel посчитал бы
     *          файл повреждённым.
     */
    void apply();

    /// @brief Был ли документ изменён в @ref apply.
    bool modified() const { return m_modified; }

    /// @brief Текущий XML листа.
    std::string serialize() const;

    /// @brief Имя листа.
    const std::string& name() const { return m_name; }
    /// @brief Начальная строка.
    std::uint32_t startRow() const { return m_startRow; }
    /// @brief Статистика; полностью заполнена после @ref apply.
    const SheetStats& stats() const { return m_stats; }

private:
    /// @brief Строка-кандидат.
    struct RowRef
    {
        pugi::xml_node node;      ///< Элемент @c &lt;row&gt;.
        std::uint32_t number;     ///< Номер строки.
    };

    std::string m_name;
    std::uint32_t m_startRow;
    pugi::xml_document m_doc;
    pugi::xml_node m_sheetData;
    std::vector<RowRef> m_rows;
    std::vector<char> m_redFlags;    ///< @c char, а не @c bool: потоки пишут в соседние элементы.
    SheetStats m_stats;
    bool m_modified = false;
};

/// @brief Результат однопоточной обработки листа.
struct SheetResult
{
    std::string xml;          ///< Итоговый XML (исходный, если изменений нет).
    bool modified = false;    ///< Были ли изменения.
    SheetStats stats;         ///< Статистика.
};

/**
 * @brief Обрабатывает лист целиком в одном потоке.
 * @param xml Текст части листа.
 * @param startRow Начальная строка.
 * @param styles Таблица красных стилей.
 * @return Результат обработки.
 */
SheetResult processSheetXml(const std::string& xml, std::uint32_t startRow, const RedStyleTable& styles);

}    // namespace redclean
