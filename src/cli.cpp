/**
 * @file cli.cpp
 * @brief Разбор аргументов командной строки и текст справки.
 */

#include "cli.hpp"

#include <charconv>
#include <limits>
#include <optional>
#include <string_view>

namespace redclean {

namespace {

/**
 * @brief Разбирает целое число в заданном диапазоне.
 * @tparam T Целочисленный тип.
 * @param text Текст значения.
 * @param option Имя опции для сообщения об ошибке.
 * @param minValue Минимум (включительно).
 * @param maxValue Максимум (включительно).
 * @return Число.
 * @throws CliError Если текст не число или вне диапазона.
 */
template <typename T>
T parseNumber(const std::string& text, const std::string& option, T minValue, T maxValue)
{
    T value{};
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || value < minValue || value > maxValue)
        throw CliError("Некорректное значение для " + option + ": \"" + text + "\"");
    return value;
}

/**
 * @brief Разбирает вещественное число в заданном диапазоне.
 * @param text Текст значения.
 * @param option Имя опции для сообщения об ошибке.
 * @param minValue Минимум (включительно).
 * @param maxValue Максимум (включительно).
 * @return Число.
 * @throws CliError Если текст не число или вне диапазона.
 */
double parseDouble(const std::string& text, const std::string& option, double minValue, double maxValue)
{
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size() && value >= minValue && value <= maxValue) return value;
    }
    catch (const std::exception&) {
    }
    throw CliError("Некорректное значение для " + option + ": \"" + text + "\"");
}

/**
 * @brief Разбирает значение @c --sheet.
 * @details Формат @c "Имя" или @c "Имя:строка". Двоеточие в именах листов Excel
 *          запрещено, поэтому разбор однозначен.
 * @param text Значение опции.
 * @return Описание листа.
 * @throws CliError Если имя пустое или номер строки некорректен.
 */
SheetSpec parseSheetSpec(const std::string& text)
{
    SheetSpec spec;
    const auto colon = text.rfind(':');
    if (colon == std::string::npos) {
        spec.name = text;
    }
    else {
        spec.name = text.substr(0, colon);
        spec.startRow = parseNumber<std::uint32_t>(text.substr(colon + 1), "--sheet", 1, 1048576);
    }
    if (spec.name.empty()) throw CliError("Пустое имя листа в --sheet \"" + text + "\"");
    return spec;
}

/**
 * @brief Разбирает список цветов @c RRGGBB через запятую.
 * @param text Значение @c --colors.
 * @return Цвета в порядке перечисления.
 * @throws CliError Если какой-то цвет некорректен.
 */
std::vector<Rgb> parseColorList(const std::string& text)
{
    std::vector<Rgb> colors;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto comma = text.find(',', pos);
        const std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const auto color = parseHexColor(item);
        if (!color) throw CliError("Некорректный цвет \"" + item + "\" (ожидается RRGGBB)");
        colors.push_back(*color);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return colors;
}

/**
 * @brief Разбирает значение @c --target.
 * @param text @c fill, @c font или @c any.
 * @return Что проверять у ячейки.
 * @throws CliError Для других значений.
 */
ColorTarget parseTarget(const std::string& text)
{
    if (text == "fill") return ColorTarget::Fill;
    if (text == "font") return ColorTarget::Font;
    if (text == "any") return ColorTarget::Any;
    throw CliError("--target принимает fill, font или any, получено \"" + text + "\"");
}

}    // namespace

CliCommand parseCommandLine(const std::vector<std::string>& args)
{
    CliCommand cmd;
    CleanOptions& opt = cmd.options;
    bool inPlace = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string arg = args[i];
        std::optional<std::string> inlineValue;
        if (arg.rfind("--", 0) == 0) {
            const auto eq = arg.find('=');
            if (eq != std::string::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
            }
        }
        auto value = [&]() -> std::string {
            if (inlineValue) return *inlineValue;
            if (i + 1 >= args.size()) throw CliError("Для " + arg + " нужно значение");
            return args[++i];
        };
        auto noValue = [&] {
            if (inlineValue) throw CliError("Опция " + arg + " не принимает значение");
        };

        if (arg == "-h" || arg == "--help") {
            noValue();
            cmd.showHelp = true;
        }
        else if (arg == "-i" || arg == "--input") opt.inputPath = value();
        else if (arg == "-o" || arg == "--output") opt.outputPath = value();
        else if (arg == "--in-place") { noValue(); inPlace = true; }
        else if (arg == "-s" || arg == "--sheet") opt.sheets.push_back(parseSheetSpec(value()));
        else if (arg == "--all-sheets") { noValue(); opt.allSheets = true; }
        else if (arg == "-r" || arg == "--start-row")
            opt.defaultStartRow = parseNumber<std::uint32_t>(value(), arg, 1, 1048576);
        else if (arg == "-j" || arg == "--threads")
            opt.threads = parseNumber<std::size_t>(value(), arg, 1, 1024);
        else if (arg == "--rows-per-task")
            opt.rowsPerTask = parseNumber<std::size_t>(value(), arg, 1, std::numeric_limits<std::uint32_t>::max());
        else if (arg == "--compression-level")
            opt.compressionLevel = parseNumber<int>(value(), arg, 1, 9);
        else if (arg == "-t" || arg == "--target") opt.style.target = parseTarget(value());
        else if (arg == "--colors") opt.style.criteria.exactColors = parseColorList(value());
        else if (arg == "--hue-tolerance") opt.style.criteria.maxHueDeviation = parseDouble(value(), arg, 0.0, 180.0);
        else if (arg == "--min-saturation") opt.style.criteria.minSaturation = parseDouble(value(), arg, 0.0, 1.0);
        else if (arg == "--min-value") opt.style.criteria.minValue = parseDouble(value(), arg, 0.0, 1.0);
        else if (arg == "--dry-run") { noValue(); opt.dryRun = true; }
        else if (arg == "-v" || arg == "--verbose") { noValue(); cmd.verbose = true; }
        else if (!arg.empty() && arg.front() == '-') throw CliError("Неизвестная опция: " + arg);
        else if (opt.inputPath.empty()) opt.inputPath = arg;
        else throw CliError("Лишний аргумент: " + arg);
    }

    if (cmd.showHelp) return cmd;

    if (opt.inputPath.empty()) throw CliError("Не указан входной файл (-i)");
    if (opt.allSheets && !opt.sheets.empty()) throw CliError("--all-sheets нельзя совмещать с --sheet");
    if (!opt.allSheets && opt.sheets.empty()) throw CliError("Укажите листы через --sheet (или --all-sheets)");
    if (inPlace && !opt.outputPath.empty()) throw CliError("--in-place нельзя совмещать с --output");
    if (inPlace) opt.outputPath = opt.inputPath;
    if (opt.outputPath.empty() && !opt.dryRun) throw CliError("Укажите выходной файл (-o), --in-place или --dry-run");
    return cmd;
}

std::string usageText(const std::string& programName)
{
    return "Использование: " + programName + R"( -i ВХОД.xlsx (-o ВЫХОД.xlsx | --in-place | --dry-run) -s ЛИСТ[:СТРОКА]... [опции]

Очищает содержимое (значения и формулы, форматирование сохраняется) всех строк, начиная
с заданной, в которых есть хотя бы одна ячейка красного цвета.

Файлы и листы:
  -i, --input FILE        входной файл .xlsx (можно указать позиционно)
  -o, --output FILE       куда сохранить результат
      --in-place          перезаписать входной файл
  -s, --sheet NAME[:ROW]  лист для обработки, можно повторять; ROW — своя начальная строка
      --all-sheets        обработать все рабочие листы
  -r, --start-row N       начальная строка по умолчанию (1)

Что считать красным:
  -t, --target WHAT       fill (заливка, по умолчанию) | font (цвет шрифта) | any
      --colors LIST       точный список цветов RRGGBB через запятую, например FF0000,C00000
      --hue-tolerance D   допуск оттенка от чистого красного в градусах (20)
      --min-saturation X  минимальная насыщенность 0..1 (0.5)
      --min-value X       минимальная яркость 0..1 (0.5)

Производительность и прочее:
  -j, --threads N         число потоков (по умолчанию — число ядер)
      --rows-per-task N   строк в одной задаче поиска (4096)
      --compression-level N  уровень сжатия 1..9 (6); 1 — заметно быстрее, файл чуть больше
      --dry-run           только показать, какие строки будут очищены
  -v, --verbose           вывести номера найденных строк и время этапов
  -h, --help              эта справка
)";
}

}    // namespace redclean
