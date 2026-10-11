/**
 * @file formula.cpp
 * @brief Реализация разбора адресов и сдвига ссылок в формулах.
 */

#include "formula.hpp"

#include <cctype>

namespace redclean {

namespace {

/**
 * @brief Символ, который может входить в «слово» формулы: ссылку, имя, число.
 * @param c Символ; байты UTF-8 ≥ 0x80 тоже считаются частью слова.
 * @return @c true для символов слова.
 */
bool isWordChar(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '.' || c == '$' || c == '\\' || u >= 0x80;
}

/// @brief Латинская буква.
bool isLetter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
/// @brief Десятичная цифра.
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

/// @brief Часть ссылки: номер строки или столбца и признак абсолютности.
struct RefPart
{
    bool absolute = false;      ///< Был ли знак @c $.
    std::uint32_t value = 0;    ///< Номер строки или столбца.
};

/**
 * @brief Разбирает часть-столбец @c "$?LETTERS" целиком.
 * @param s Текст.
 * @return Часть ссылки или @c std::nullopt.
 */
std::optional<RefPart> parseColumnPart(std::string_view s)
{
    RefPart part;
    if (!s.empty() && s.front() == '$') {
        part.absolute = true;
        s.remove_prefix(1);
    }
    if (s.empty() || s.size() > 3) return std::nullopt;
    for (char c : s)
        if (!isLetter(c)) return std::nullopt;
    part.value = columnFromLetters(s);
    if (part.value == 0) return std::nullopt;
    return part;
}

/**
 * @brief Разбирает часть-строку @c "$?DIGITS" целиком.
 * @param s Текст.
 * @return Часть ссылки или @c std::nullopt.
 */
std::optional<RefPart> parseRowPart(std::string_view s)
{
    RefPart part;
    if (!s.empty() && s.front() == '$') {
        part.absolute = true;
        s.remove_prefix(1);
    }
    if (s.empty() || s.size() > 7) return std::nullopt;
    std::uint64_t value = 0;
    for (char c : s) {
        if (!isDigit(c)) return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value == 0 || value > kMaxRows) return std::nullopt;
    part.value = static_cast<std::uint32_t>(value);
    return part;
}

/// @brief Разобранная ссылка на ячейку.
struct CellRefParts
{
    RefPart col;    ///< Столбец.
    RefPart row;    ///< Строка.
};

/**
 * @brief Разбирает ссылку на ячейку вида @c "$A$1" целиком.
 * @param s Текст.
 * @return Части ссылки или @c std::nullopt.
 */
std::optional<CellRefParts> parseCellParts(std::string_view s)
{
    std::size_t i = 0;
    if (i < s.size() && s[i] == '$') ++i;
    while (i < s.size() && isLetter(s[i])) ++i;
    const auto col = parseColumnPart(s.substr(0, i));
    if (!col) return std::nullopt;
    const auto row = parseRowPart(s.substr(i));
    if (!row) return std::nullopt;
    return CellRefParts{*col, *row};
}

/**
 * @brief Сдвигает часть ссылки, если она относительная.
 * @param part Часть ссылки.
 * @param delta Сдвиг.
 * @param limit Максимально допустимое значение.
 * @return Новое значение или @c std::nullopt, если ссылка ушла за пределы листа.
 */
std::optional<std::uint32_t> shifted(const RefPart& part, std::int64_t delta, std::uint32_t limit)
{
    if (part.absolute) return part.value;
    const std::int64_t v = static_cast<std::int64_t>(part.value) + delta;
    if (v < 1 || v > static_cast<std::int64_t>(limit)) return std::nullopt;
    return static_cast<std::uint32_t>(v);
}

/// @brief Форматирует столбец с сохранением знака @c $.
std::string formatColumn(const RefPart& part, std::uint32_t value)
{
    return (part.absolute ? "$" : "") + columnToLetters(value);
}

/// @brief Форматирует строку с сохранением знака @c $.
std::string formatRow(const RefPart& part, std::uint32_t value)
{
    return (part.absolute ? "$" : "") + std::to_string(value);
}

/**
 * @brief Пропускает литерал в кавычках с учётом удвоенных кавычек внутри.
 * @param s Формула.
 * @param i Позиция открывающей кавычки.
 * @param quote Символ кавычки (@c '"' или @c '\'').
 * @return Позиция после закрывающей кавычки.
 */
std::size_t skipQuoted(std::string_view s, std::size_t i, char quote)
{
    ++i;
    while (i < s.size()) {
        if (s[i] == quote) {
            if (i + 1 < s.size() && s[i + 1] == quote) {
                i += 2;
                continue;
            }
            return i + 1;
        }
        ++i;
    }
    return i;
}

/**
 * @brief Пропускает выражение в квадратных скобках с учётом вложенности.
 * @param s Формула.
 * @param i Позиция открывающей скобки.
 * @return Позиция после парной закрывающей скобки.
 */
std::size_t skipBrackets(std::string_view s, std::size_t i)
{
    int depth = 0;
    while (i < s.size()) {
        if (s[i] == '[') ++depth;
        if (s[i] == ']' && --depth == 0) return i + 1;
        ++i;
    }
    return i;
}

/// @brief Позиция конца слова, начинающегося в @p i.
std::size_t wordEnd(std::string_view s, std::size_t i)
{
    while (i < s.size() && isWordChar(s[i])) ++i;
    return i;
}

}    // namespace

std::uint32_t columnFromLetters(std::string_view letters)
{
    if (letters.empty() || letters.size() > 3) return 0;
    std::uint32_t value = 0;
    for (char c : letters) {
        if (!isLetter(c)) return 0;
        value = value * 26 + static_cast<std::uint32_t>(std::toupper(static_cast<unsigned char>(c)) - 'A' + 1);
    }
    return value <= kMaxColumns ? value : 0;
}

std::string columnToLetters(std::uint32_t column)
{
    std::string out;
    while (column > 0) {
        const std::uint32_t rem = (column - 1) % 26;
        out.insert(out.begin(), static_cast<char>('A' + rem));
        column = (column - 1) / 26;
    }
    return out;
}

std::optional<CellPos> parseCellRef(std::string_view ref)
{
    const auto parts = parseCellParts(ref);
    if (!parts) return std::nullopt;
    return CellPos{parts->row.value, parts->col.value};
}

std::string shiftFormula(std::string_view f, std::int64_t dRow, std::int64_t dCol)
{
    std::string out;
    out.reserve(f.size() + 8);

    std::size_t i = 0;
    while (i < f.size()) {
        const char c = f[i];
        if (c == '"' || c == '\'') {
            const std::size_t end = skipQuoted(f, i, c);
            out.append(f.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '[') {
            const std::size_t end = skipBrackets(f, i);
            out.append(f.substr(i, end - i));
            i = end;
            continue;
        }
        if (!isWordChar(c)) {
            out.push_back(c);
            ++i;
            continue;
        }

        const std::size_t end = wordEnd(f, i);
        const std::string_view word = f.substr(i, end - i);
        const char next = end < f.size() ? f[end] : '\0';

        if (next == '(' || next == '!') {
            out.append(word);
            i = end;
            continue;
        }

        if (const auto cell = parseCellParts(word)) {
            const auto col = shifted(cell->col, dCol, kMaxColumns);
            const auto row = shifted(cell->row, dRow, kMaxRows);
            if (col && row)
                out += formatColumn(cell->col, *col) + formatRow(cell->row, *row);
            else
                out += "#REF!";
            i = end;
            continue;
        }

        if (next == ':' && end + 1 < f.size() && isWordChar(f[end + 1])) {
            const std::size_t end2 = wordEnd(f, end + 1);
            const std::string_view word2 = f.substr(end + 1, end2 - end - 1);
            const char after = end2 < f.size() ? f[end2] : '\0';
            if (after != '(' && after != '!') {
                const auto c1 = parseColumnPart(word), c2 = parseColumnPart(word2);
                if (c1 && c2) {
                    const auto s1 = shifted(*c1, dCol, kMaxColumns), s2 = shifted(*c2, dCol, kMaxColumns);
                    out += (s1 && s2) ? formatColumn(*c1, *s1) + ":" + formatColumn(*c2, *s2) : "#REF!";
                    i = end2;
                    continue;
                }
                const auto r1 = parseRowPart(word), r2 = parseRowPart(word2);
                if (r1 && r2) {
                    const auto s1 = shifted(*r1, dRow, kMaxRows), s2 = shifted(*r2, dRow, kMaxRows);
                    out += (s1 && s2) ? formatRow(*r1, *s1) + ":" + formatRow(*r2, *s2) : "#REF!";
                    i = end2;
                    continue;
                }
            }
        }

        out.append(word);
        i = end;
    }
    return out;
}

}    // namespace redclean
