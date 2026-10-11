/**
 * @file main.cpp
 * @brief Точка входа консольной утилиты @c xlsx_red_cleaner.
 */

#include "cleaner.hpp"
#include "cli.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <shellapi.h>
#endif

namespace {

/**
 * @brief Сворачивает отсортированный список номеров строк в диапазоны.
 * @param rows Номера строк по возрастанию.
 * @return Строка вида @c "3, 5-9, 12".
 */
std::string formatRows(const std::vector<std::uint32_t>& rows)
{
    std::string out;
    for (std::size_t i = 0; i < rows.size();) {
        std::size_t j = i;
        while (j + 1 < rows.size() && rows[j + 1] == rows[j] + 1) ++j;
        if (!out.empty()) out += ", ";
        out += std::to_string(rows[i]);
        if (j > i) out += "-" + std::to_string(rows[j]);
        i = j + 1;
    }
    return out;
}

/**
 * @brief Возвращает аргументы командной строки в UTF-8.
 * @details В Windows @c argv приходит в ANSI-кодировке, поэтому аргументы берутся
 *          через @c CommandLineToArgvW и перекодируются; заодно консоль переключается на UTF-8.
 *          На остальных системах аргументы используются как есть.
 * @param argc Число аргументов из @c main.
 * @param argv Аргументы из @c main.
 * @return Все аргументы, включая имя программы.
 */
std::vector<std::string> utf8Arguments(int argc, char* argv[])
{
#ifdef _WIN32
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> args;
    for (int i = 0; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size > 0 ? static_cast<std::size_t>(size - 1) : 0, '\0');
        if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
        args.push_back(std::move(arg));
    }
    LocalFree(wide);
    return args;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

}    // namespace

/**
 * @brief Разбирает аргументы, запускает очистку и печатает отчёт.
 * @return 0 — успех, 1 — ошибка обработки файла, 2 — ошибка в аргументах.
 */
int main(int argc, char* argv[])
{
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    const std::string program =
        args.empty() ? "xlsx_red_cleaner" : std::filesystem::u8path(args.front()).filename().u8string();

    redclean::CliCommand cmd;
    try {
        cmd = redclean::parseCommandLine(std::vector<std::string>(args.begin() + (args.empty() ? 0 : 1), args.end()));
    }
    catch (const redclean::CliError& e) {
        std::cerr << "Ошибка: " << e.what() << "\n\n" << redclean::usageText(program);
        return 2;
    }
    if (cmd.showHelp) {
        std::cout << redclean::usageText(program);
        return 0;
    }

    try {
        const auto started = std::chrono::steady_clock::now();
        const redclean::CleanReport report = redclean::cleanWorkbook(cmd.options);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        std::cout << "Стилей ячеек: " << report.styleCount << ", из них красных: " << report.redStyleCount
                  << "; потоков: " << report.threadsUsed << "\n";
        for (const auto& sheet : report.sheets) {
            const auto& s = sheet.stats;
            std::cout << "Лист \"" << sheet.name << "\" (со строки " << sheet.startRow << "): просмотрено строк "
                      << s.rowsScanned << ", строк с красными ячейками " << s.redRows.size() << ", очищено ячеек "
                      << s.cellsCleared;
            if (s.sharedFormulasExpanded > 0)
                std::cout << ", развёрнуто общих формул " << s.sharedFormulasExpanded;
            std::cout << "\n";
            if (cmd.verbose && !s.redRows.empty()) std::cout << "  строки: " << formatRows(s.redRows) << "\n";
        }
        if (cmd.options.dryRun)
            std::cout << "Пробный запуск: файл не изменён.\n";
        else if (report.written)
            std::cout << "Сохранено в \"" << cmd.options.outputPath << "\""
                      << (report.calcChainRemoved ? " (цепочка вычислений будет пересоздана Excel)" : "") << "\n";
        else
            std::cout << "Изменений нет, файл не перезаписан.\n";
        if (cmd.verbose)
            for (const auto& [stage, seconds] : report.stageSeconds) std::cout << "  " << stage << ": " << seconds << " с\n";
        std::cout << "Время: " << elapsed << " с\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
}
