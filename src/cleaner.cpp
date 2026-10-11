/**
 * @file cleaner.cpp
 * @brief Реализация cleanWorkbook: параллельные этапы обработки книги.
 */

#include "cleaner.hpp"

#include "thread_pool.hpp"
#include "xlsx_package.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>

namespace redclean {

namespace {

/// @brief Состояние обработки одного листа.
struct SheetTask
{
    SheetInfo info;                               ///< Лист в книге.
    std::unique_ptr<SheetProcessor> processor;    ///< Разобранный лист.
    std::string resultXml;                        ///< Новый XML листа, если он изменён.
};

/// @brief Замеряет длительность этапов и складывает её в CleanReport::stageSeconds.
class StageTimer
{
public:
    /// @brief Начинает отсчёт первого этапа.
    explicit StageTimer(CleanReport& report) : m_report(report) {}

    /// @brief Завершает текущий этап под именем @p stage и начинает следующий.
    void mark(const char* stage)
    {
        const auto now = std::chrono::steady_clock::now();
        m_report.stageSeconds.emplace_back(stage, std::chrono::duration<double>(now - m_last).count());
        m_last = now;
    }

private:
    CleanReport& m_report;
    std::chrono::steady_clock::time_point m_last = std::chrono::steady_clock::now();
};

/**
 * @brief Определяет листы для обработки.
 * @param package Открытая книга.
 * @param options Параметры; при CleanOptions::allSheets берутся все рабочие листы.
 * @return Задачи по листам в порядке перечисления.
 * @throws std::invalid_argument Если лист не найден, указан дважды или не указан ни один.
 */
std::vector<SheetTask> resolveSheets(const XlsxPackage& package, const CleanOptions& options)
{
    std::vector<SheetTask> tasks;
    std::set<std::string> seen;
    auto add = [&](const SheetInfo& info, std::uint32_t startRow) {
        if (!seen.insert(info.name).second) throw std::invalid_argument("Лист \"" + info.name + "\" указан дважды");
        tasks.push_back({info, std::make_unique<SheetProcessor>(info.name, startRow), {}});
    };

    if (options.allSheets) {
        for (const auto& info : package.worksheets()) add(info, options.defaultStartRow);
        return tasks;
    }
    if (options.sheets.empty()) throw std::invalid_argument("Не указан ни один лист");

    for (const auto& spec : options.sheets) {
        const auto info = package.findWorksheet(spec.name);
        if (!info) {
            std::string available;
            for (const auto& ws : package.worksheets()) available += (available.empty() ? "" : ", ") + ("\"" + ws.name + "\"");
            throw std::invalid_argument("Лист \"" + spec.name + "\" не найден. Доступные листы: " + available);
        }
        add(*info, spec.startRow.value_or(options.defaultStartRow));
    }
    return tasks;
}

}    // namespace

CleanReport cleanWorkbook(const CleanOptions& options)
{
    if (options.inputPath.empty()) throw std::invalid_argument("Не указан входной файл");
    if (!options.dryRun && options.outputPath.empty()) throw std::invalid_argument("Не указан выходной файл");
    if (options.defaultStartRow == 0) throw std::invalid_argument("Номер начальной строки должен быть >= 1");

    CleanReport report;
    StageTimer timer(report);

    XlsxPackage package(options.inputPath);
    std::vector<SheetTask> tasks = resolveSheets(package, options);
    timer.mark("открытие архива");

    const RedStyleTable styles =
        RedStyleTable::build(package.read(package.stylesPath()), package.read(package.themePath()), options.style);
    report.styleCount = styles.styleCount();
    report.redStyleCount = styles.redCount();

    ThreadPool pool(options.threads);
    report.threadsUsed = pool.size();

    {
        std::vector<std::future<void>> futures;
        for (auto& task : tasks)
            futures.push_back(pool.submit([&task, &package] { task.processor->load(package.read(task.info.path)); }));
        waitAll(futures);
    }
    timer.mark("распаковка и разбор XML");

    if (report.redStyleCount > 0) {
        std::vector<std::future<void>> futures;
        const std::size_t grain = std::max<std::size_t>(1, options.rowsPerTask);
        for (auto& task : tasks) {
            SheetProcessor* processor = task.processor.get();
            for (std::size_t begin = 0; begin < processor->candidateRowCount(); begin += grain)
                futures.push_back(
                    pool.submit([processor, begin, grain, &styles] { processor->detect(begin, begin + grain, styles); }));
        }
        waitAll(futures);
    }
    timer.mark("поиск красных строк");

    {
        std::vector<std::future<void>> futures;
        const bool dryRun = options.dryRun;
        for (auto& task : tasks)
            futures.push_back(pool.submit([&task, dryRun] {
                task.processor->apply();
                if (!dryRun && task.processor->modified()) task.resultXml = task.processor->serialize();
            }));
        waitAll(futures);
    }
    timer.mark("очистка и сериализация");

    bool formulasRemoved = false;
    bool anyModified = false;
    for (auto& task : tasks) {
        report.sheets.push_back({task.processor->name(), task.processor->startRow(), task.processor->stats()});
        formulasRemoved = formulasRemoved || task.processor->stats().formulasRemoved > 0;
        if (task.processor->modified()) {
            anyModified = true;
            if (!options.dryRun) package.write(task.info.path, std::move(task.resultXml));
        }
    }

    if (options.dryRun) return report;

    if (formulasRemoved) report.calcChainRemoved = package.removeCalcChain();
    if (anyModified || options.outputPath != options.inputPath) {
        package.saveAs(options.outputPath, pool, options.compressionLevel);
        report.written = true;
    }
    timer.mark("сжатие и запись");
    return report;
}

}    // namespace redclean
