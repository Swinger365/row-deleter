/**
 * @file cli.hpp
 * @brief Командная строка программы.
 */

#pragma once

#include "cleaner.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace redclean {

/// @brief Ошибка в аргументах командной строки.
class CliError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// @brief Результат разбора командной строки.
struct CliCommand
{
    CleanOptions options;      ///< Параметры очистки.
    bool showHelp = false;     ///< Запрошена справка; остальные поля не заполняются.
    bool verbose = false;      ///< Печатать длительность этапов.
};

/**
 * @brief Разбирает аргументы командной строки.
 * @details Принимает опции в виде @c "--opt value" и @c "--opt=value";
 *          входной файл можно указать позиционно.
 * @param args Аргументы без имени программы, в UTF-8.
 * @return Разобранная команда.
 * @throws CliError При некорректном вводе.
 */
CliCommand parseCommandLine(const std::vector<std::string>& args);

/**
 * @brief Текст справки.
 * @param programName Имя исполняемого файла для строки использования.
 * @return Многострочный текст.
 */
std::string usageText(const std::string& programName);

}    // namespace redclean
