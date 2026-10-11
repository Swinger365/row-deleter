/**
 * @file xlsx_package.hpp
 * @brief Пакет .xlsx (Office Open XML): связи книги, листы и части.
 */

#pragma once

#include "thread_pool.hpp"
#include "zip_archive.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace redclean {

/// @brief Рабочий лист книги.
struct SheetInfo
{
    std::string name;    ///< Имя листа, как в Excel.
    std::string path;    ///< Путь части в архиве, например @c "xl/worksheets/sheet1.xml".
};

/**
 * @brief Книга .xlsx как набор частей.
 * @details Разбирает @c _rels/.rels, @c workbook.xml и его связи, чтобы найти листы,
 *          стили, тему и цепочку вычислений. Изменённые части хранятся в памяти до
 *          @ref saveAs; все остальные при сохранении копируются байт в байт.
 *
 * Потокобезопасность: @ref read можно вызывать одновременно из разных потоков,
 * если в это время никто не вызывает изменяющие методы; остальные методы — из одного потока.
 */
class XlsxPackage
{
public:
    /**
     * @brief Открывает книгу.
     * @param path Путь к файлу в UTF-8.
     * @throws std::runtime_error Если файла нет, он не zip-архив или не книга Excel.
     */
    explicit XlsxPackage(const std::string& path);

    /// @brief Рабочие листы в порядке книги.
    const std::vector<SheetInfo>& worksheets() const { return m_worksheets; }

    /**
     * @brief Ищет рабочий лист по имени.
     * @param name Имя листа.
     * @return Лист или @c std::nullopt, если такого нет.
     * @throws std::runtime_error Если лист с таким именем есть, но это не лист с ячейками
     *         (например, лист диаграммы).
     */
    std::optional<SheetInfo> findWorksheet(const std::string& name) const;

    /// @brief Путь части стилей; пусто, если её нет.
    const std::string& stylesPath() const { return m_stylesPath; }
    /// @brief Путь части темы; пусто, если её нет.
    const std::string& themePath() const { return m_themePath; }

    /**
     * @brief Содержимое части с учётом изменений.
     * @param partPath Путь части в архиве.
     * @return Содержимое или пустая строка, если части нет.
     */
    std::string read(const std::string& partPath) const;

    /**
     * @brief Заменяет или добавляет часть.
     * @param partPath Путь части в архиве.
     * @param data Новое содержимое.
     */
    void write(const std::string& partPath, std::string data);

    /**
     * @brief Удаляет @c xl/calcChain.xml вместе со ссылками на него.
     * @details Нужно, когда удалялись формулы: устаревшая цепочка вычислений заставила бы
     *          Excel сообщить о повреждении. Excel пересоздаст её при открытии.
     * @return @c true, если цепочка была и удалена.
     */
    bool removeCalcChain();

    /**
     * @brief Сохраняет книгу.
     * @details Изменённые части режутся на куски по 4 МиБ, которые сжимаются параллельно
     *          в @p pool; неизменённые копируются из исходного архива без пересжатия.
     *          Порядок записей исходного архива сохраняется.
     * @param path Путь в UTF-8; может совпадать с исходным.
     * @param pool Пул потоков для сжатия.
     * @param compressionLevel Уровень deflate 1..9.
     * @throws std::runtime_error При ошибке записи.
     */
    void saveAs(const std::string& path, ThreadPool& pool, int compressionLevel) const;

private:
    ZipReader m_zip;
    std::map<std::string, std::string> m_replaced;
    std::set<std::string> m_removed;
    std::string m_workbookPath;
    std::string m_workbookRelsPath;
    std::string m_stylesPath;
    std::string m_themePath;
    std::string m_calcChainPath;
    std::vector<SheetInfo> m_worksheets;
    std::vector<std::string> m_otherSheetNames;    ///< Листы диаграмм, макросов и т.п.
};

}    // namespace redclean
