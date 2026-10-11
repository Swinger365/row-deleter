/**
 * @file styles.cpp
 * @brief Реализация анализа стилей.
 */

#include "styles.hpp"

#include "xml_utils.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace redclean {

namespace {

/// @brief Всё, что нужно для перевода ссылки на цвет в RGB.
struct ColorContext
{
    ThemePalette theme;          ///< Палитра темы.
    std::vector<Rgb> indexed;    ///< Пользовательская палитра из @c &lt;indexedColors&gt;; пусто — стандартная.
};

/**
 * @brief Переводит элемент цвета (@c fgColor, @c color и т.п.) в RGB.
 * @param node Элемент цвета; может быть пустым.
 * @param ctx Палитры книги.
 * @return Цвет или @c std::nullopt для автоматического, системного или отсутствующего цвета.
 */
std::optional<Rgb> resolveColor(const pugi::xml_node& node, const ColorContext& ctx)
{
    if (!node) return std::nullopt;
    if (xml::attribute(node, "auto").as_bool(false)) return std::nullopt;

    std::optional<Rgb> base;
    if (auto rgb = xml::attribute(node, "rgb")) {
        base = parseHexColor(rgb.value());
    }
    else if (auto theme = xml::attribute(node, "theme")) {
        const unsigned idx = theme.as_uint(~0u);
        if (idx < ctx.theme.size()) base = ctx.theme[idx];
    }
    else if (auto indexed = xml::attribute(node, "indexed")) {
        const unsigned idx = indexed.as_uint(~0u);
        if (idx < ctx.indexed.size())
            base = ctx.indexed[idx];
        else
            base = legacyIndexedColor(idx);
    }
    if (!base) return std::nullopt;

    const double tint = xml::attribute(node, "tint").as_double(0.0);
    return applyTint(*base, tint);
}

/**
 * @brief Проверяет, что элемент цвета задаёт красный цвет.
 * @param node Элемент цвета.
 * @param ctx Палитры книги.
 * @param criteria Критерий красного.
 * @return @c true для красного цвета.
 */
bool isRedColor(const pugi::xml_node& node, const ColorContext& ctx, const RedCriteria& criteria)
{
    const auto color = resolveColor(node, ctx);
    return color && criteria.matches(*color);
}

/**
 * @brief Проверяет заливку (@c &lt;fill&gt;).
 * @details У сплошной заливки проверяется @c fgColor; у узорной виден и фон узора,
 *          поэтому проверяется ещё @c bgColor; у градиентной — каждая опорная точка.
 * @param fill Элемент @c &lt;fill&gt;.
 * @param ctx Палитры книги.
 * @param criteria Критерий красного.
 * @return @c true, если заливка красная.
 */
bool isRedFill(const pugi::xml_node& fill, const ColorContext& ctx, const RedCriteria& criteria)
{
    if (const auto pattern = xml::child(fill, "patternFill")) {
        const std::string_view type = xml::attribute(pattern, "patternType").as_string("none");
        if (type == "none") return false;
        if (isRedColor(xml::child(pattern, "fgColor"), ctx, criteria)) return true;
        return type != "solid" && isRedColor(xml::child(pattern, "bgColor"), ctx, criteria);
    }
    if (const auto gradient = xml::child(fill, "gradientFill")) {
        bool red = false;
        xml::forEachChild(gradient, "stop", [&](const pugi::xml_node& stop) {
            red = red || isRedColor(xml::child(stop, "color"), ctx, criteria);
        });
        return red;
    }
    return false;
}

/**
 * @brief Вычисляет предикат для каждого элемента списка (@c &lt;fills&gt;, @c &lt;fonts&gt;, @c &lt;cellXfs&gt;).
 * @tparam Predicate Вызываемый объект @c bool(const pugi::xml_node&).
 * @param list Элемент-список.
 * @param itemName Локальное имя элементов списка.
 * @param predicate Предикат.
 * @return Флаги в порядке элементов, то есть по индексам.
 */
template <typename Predicate>
std::vector<bool> evaluateList(const pugi::xml_node& list, std::string_view itemName, Predicate&& predicate)
{
    std::vector<bool> out;
    xml::forEachChild(list, itemName, [&](const pugi::xml_node& item) { out.push_back(predicate(item)); });
    return out;
}

/**
 * @brief Флаг по индексу из атрибута (@c fillId, @c fontId).
 * @param flags Флаги.
 * @param indexAttr Атрибут с индексом.
 * @return Значение флага; @c false для отсутствующего атрибута или индекса вне диапазона.
 */
bool flagAt(const std::vector<bool>& flags, const pugi::xml_attribute& indexAttr)
{
    if (!indexAttr) return false;
    const unsigned idx = indexAttr.as_uint(~0u);
    return idx < flags.size() && flags[idx];
}

}    // namespace

ThemePalette defaultThemePalette()
{
    return {Rgb{0xFF, 0xFF, 0xFF}, Rgb{0x00, 0x00, 0x00}, Rgb{0xE7, 0xE6, 0xE6}, Rgb{0x44, 0x54, 0x6A},
            Rgb{0x44, 0x72, 0xC4}, Rgb{0xED, 0x7D, 0x31}, Rgb{0xA5, 0xA5, 0xA5}, Rgb{0xFF, 0xC0, 0x00},
            Rgb{0x5B, 0x9B, 0xD5}, Rgb{0x70, 0xAD, 0x47}, Rgb{0x05, 0x63, 0xC1}, Rgb{0x95, 0x4F, 0x72}};
}

ThemePalette parseThemePalette(const std::string& themeXml)
{
    ThemePalette palette = defaultThemePalette();
    if (themeXml.empty()) return palette;

    pugi::xml_document doc;
    xml::load(doc, themeXml, "тема");
    const auto scheme = xml::child(xml::child(doc.document_element(), "themeElements"), "clrScheme");
    if (!scheme) return palette;

    static const std::pair<std::string_view, std::size_t> kSlots[] = {
        {"lt1", 0},      {"dk1", 1},      {"lt2", 2},      {"dk2", 3},       {"accent1", 4},  {"accent2", 5},
        {"accent3", 6},  {"accent4", 7},  {"accent5", 8},  {"accent6", 9},   {"hlink", 10},   {"folHlink", 11},
    };
    for (const auto& [name, index] : kSlots) {
        const auto slot = xml::child(scheme, name);
        std::optional<Rgb> color;
        if (const auto srgb = xml::child(slot, "srgbClr")) {
            color = parseHexColor(xml::attribute(srgb, "val").value());
        }
        else if (const auto sys = xml::child(slot, "sysClr")) {
            color = parseHexColor(xml::attribute(sys, "lastClr").value());
            if (!color) {
                const std::string_view val = xml::attribute(sys, "val").value();
                if (val == "windowText") color = Rgb{0, 0, 0};
                if (val == "window") color = Rgb{0xFF, 0xFF, 0xFF};
            }
        }
        if (color) palette[index] = *color;
    }
    return palette;
}

RedStyleTable::RedStyleTable(std::vector<bool> redFlags) : m_red(std::move(redFlags)) {}

RedStyleTable RedStyleTable::build(const std::string& stylesXml, const std::string& themeXml, const StyleOptions& options)
{
    if (stylesXml.empty()) return RedStyleTable{};

    pugi::xml_document doc;
    xml::load(doc, stylesXml, "styles.xml");
    const auto root = doc.document_element();

    ColorContext ctx;
    ctx.theme = parseThemePalette(themeXml);
    xml::forEachChild(xml::child(xml::child(root, "colors"), "indexedColors"), "rgbColor", [&](const pugi::xml_node& c) {
        ctx.indexed.push_back(parseHexColor(xml::attribute(c, "rgb").value()).value_or(Rgb{}));
    });

    const RedCriteria& criteria = options.criteria;
    const bool checkFill = options.target != ColorTarget::Font;
    const bool checkFont = options.target != ColorTarget::Fill;

    std::vector<bool> redFills, redFonts;
    if (checkFill)
        redFills = evaluateList(xml::child(root, "fills"), "fill",
                                [&](const pugi::xml_node& fill) { return isRedFill(fill, ctx, criteria); });
    if (checkFont)
        redFonts = evaluateList(xml::child(root, "fonts"), "font", [&](const pugi::xml_node& font) {
            return isRedColor(xml::child(font, "color"), ctx, criteria);
        });

    std::vector<bool> red = evaluateList(xml::child(root, "cellXfs"), "xf", [&](const pugi::xml_node& xf) {
        return (checkFill && flagAt(redFills, xml::attribute(xf, "fillId"))) ||
               (checkFont && flagAt(redFonts, xml::attribute(xf, "fontId")));
    });
    return RedStyleTable(std::move(red));
}

std::size_t RedStyleTable::redCount() const { return static_cast<std::size_t>(std::count(m_red.begin(), m_red.end(), true)); }

}    // namespace redclean
