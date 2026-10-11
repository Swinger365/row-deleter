/**
 * @file cleaner.hpp
 * @brief Верхнеуровневая операция: очистка красных строк в книге.
 */

#pragma once

#include "sheet_processor.hpp"
#include "styles.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace redclean {

/// @brief Лист для обработки и строка, с которой начинать.
struct SheetSpec
{
    std::string name;                         ///< Имя листа.
    std::optional<std::uint32_t> startRow;    ///< Первая строка (с 1); если не задана — CleanOptions::defaultStartRow.
};

/// @brief Параметры очистки книги.
struct CleanOptions
{
    std::string inputPath;                ///< Исходная книга (UTF-8).
    std::string outputPath;               ///< Куда сохранить; может совпадать с inputPath.
    std::vector<SheetSpec> sheets;        ///< Листы для обработки.
    bool allSheets = false;               ///< Обработать все рабочие листы с defaultStartRow; sheets не используется.
    std::uint32_t defaultStartRow = 1;    ///< Первая строка для листов без своей.
    std::size_t threads = 0;              ///< Число потоков; 0 — по числу ядер.
    std::size_t rowsPerTask = 4096;       ///< Размер куска строк для параллельного поиска.
    int compressionLevel = 6;             ///< Уровень deflate: 1 — быстрее, 9 — меньше файл.
    StyleOptions style;                   ///< Что считать красным.
    bool dryRun = false;                  ///< Только найти строки, файл не записывать.
};

/// @brief Результат по одному листу.
struct SheetReport
{
    std::string name;                ///< Имя листа.
    std::uint32_t startRow = 1;      ///< Фактическая первая строка.
    SheetStats stats;                ///< Найденные и очищенные строки.
};

/// @brief Итог обработки книги.
struct CleanReport
{
    std::vector<SheetReport> sheets;                              ///< Результаты по листам в порядке обработки.
    std::size_t styleCount = 0;                                   ///< Число стилей ячеек (cellXfs).
    std::size_t redStyleCount = 0;                                ///< Сколько из них красные.
    std::size_t threadsUsed = 0;                                  ///< Размер пула потоков.
    bool calcChainRemoved = false;                                ///< Удалена ли цепочка вычислений.
    bool written = false;                                         ///< Был ли записан выходной файл.
    std::vector<std::pair<std::string, double>> stageSeconds;     ///< Длительность этапов в секундах.
};

/**
 * @brief Очищает содержимое строк с красными ячейками на выбранных листах.
 * @details Работает в несколько параллельных этапов:
 *          -# распаковка и разбор XML — по задаче на лист;
 *          -# поиск красных строк — куски по CleanOptions::rowsPerTask строк всех листов;
 *          -# очистка и сериализация — по задаче на лист (pugixml не допускает
 *             параллельной записи в один документ);
 *          -# сжатие изменённых частей кусками и запись архива.
 *
 *          Если ни одна строка не найдена, файл не перезаписывается (кроме случая,
 *          когда выходной путь отличается от входного — тогда книга копируется).
 * @param options Параметры.
 * @return Отчёт.
 * @throws std::invalid_argument Если параметры некорректны или лист не найден.
 * @throws std::runtime_error Если книгу не удалось прочитать или записать.
 */
CleanReport cleanWorkbook(const CleanOptions& options);

}    // namespace redclean
