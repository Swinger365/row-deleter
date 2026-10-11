/**
 * @file color.cpp
 * @brief Реализация работы с цветами.
 */

#include "color.hpp"

#include <algorithm>
#include <cmath>

namespace redclean {

namespace {

/**
 * @brief Значение шестнадцатеричной цифры.
 * @param c Символ.
 * @return 0..15 или −1, если символ не цифра.
 */
int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief Переводит канал из [0, 1] в байт с округлением и ограничением.
 * @param value Значение канала.
 * @return Байт 0..255.
 */
std::uint8_t toByte(double value)
{
    return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
}

/// @brief Цвет в модели HLS, все компоненты в [0, 1].
struct Hls
{
    double h = 0.0;    ///< Оттенок, доля оборота.
    double l = 0.0;    ///< Светлота.
    double s = 0.0;    ///< Насыщенность.
};

/**
 * @brief Переводит цвет в HLS.
 * @param c Цвет в RGB.
 * @return Цвет в HLS.
 */
Hls toHls(const Rgb& c)
{
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double maxC = std::max({r, g, b});
    const double minC = std::min({r, g, b});
    Hls out;
    out.l = (maxC + minC) / 2.0;
    const double delta = maxC - minC;
    if (delta == 0.0) return out;

    out.s = out.l <= 0.5 ? delta / (maxC + minC) : delta / (2.0 - maxC - minC);
    double h = 0.0;
    if (maxC == r)
        h = (g - b) / delta;
    else if (maxC == g)
        h = 2.0 + (b - r) / delta;
    else
        h = 4.0 + (r - g) / delta;
    h /= 6.0;
    if (h < 0.0) h += 1.0;
    out.h = h;
    return out;
}

/**
 * @brief Вспомогательная функция обратного преобразования HLS → RGB.
 * @param m1 Нижняя граница канала.
 * @param m2 Верхняя граница канала.
 * @param h Оттенок со сдвигом для конкретного канала.
 * @return Значение канала в [0, 1].
 */
double hueToChannel(double m1, double m2, double h)
{
    if (h < 0.0) h += 1.0;
    if (h > 1.0) h -= 1.0;
    if (h < 1.0 / 6.0) return m1 + (m2 - m1) * h * 6.0;
    if (h < 0.5) return m2;
    if (h < 2.0 / 3.0) return m1 + (m2 - m1) * (2.0 / 3.0 - h) * 6.0;
    return m1;
}

/**
 * @brief Переводит цвет из HLS в RGB.
 * @param c Цвет в HLS.
 * @return Цвет в RGB.
 */
Rgb fromHls(const Hls& c)
{
    if (c.s == 0.0) {
        const auto v = toByte(c.l);
        return {v, v, v};
    }
    const double m2 = c.l <= 0.5 ? c.l * (1.0 + c.s) : c.l + c.s - c.l * c.s;
    const double m1 = 2.0 * c.l - m2;
    return {toByte(hueToChannel(m1, m2, c.h + 1.0 / 3.0)), toByte(hueToChannel(m1, m2, c.h)),
            toByte(hueToChannel(m1, m2, c.h - 1.0 / 3.0))};
}

/// @brief Стандартная 64-цветная палитра Excel (индексы 0..63), значения 0xRRGGBB.
constexpr std::array<std::uint32_t, 64> kLegacyPalette = {
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080,
    0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF,
    0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF, 0x800080, 0x800000, 0x008080, 0x0000FF,
    0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
    0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696,
    0x003366, 0x339966, 0x003300, 0x333300, 0x993300, 0x993366, 0x333399, 0x333333,
};

}    // namespace

std::optional<Rgb> parseHexColor(std::string_view text)
{
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() == 8)
        text.remove_prefix(2);
    else if (text.size() != 6)
        return std::nullopt;

    std::array<std::uint8_t, 3> bytes{};
    for (std::size_t i = 0; i < 3; ++i) {
        const int hi = hexDigit(text[2 * i]);
        const int lo = hexDigit(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        bytes[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return Rgb{bytes[0], bytes[1], bytes[2]};
}

std::string toHex(const Rgb& color)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out(6, '0');
    const std::uint8_t channels[] = {color.r, color.g, color.b};
    for (std::size_t i = 0; i < 3; ++i) {
        out[2 * i] = digits[channels[i] >> 4];
        out[2 * i + 1] = digits[channels[i] & 0x0F];
    }
    return out;
}

Hsv toHsv(const Rgb& c)
{
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double maxC = std::max({r, g, b});
    const double minC = std::min({r, g, b});
    const double delta = maxC - minC;

    Hsv out;
    out.v = maxC;
    out.s = maxC == 0.0 ? 0.0 : delta / maxC;
    if (delta == 0.0) return out;

    double h = 0.0;
    if (maxC == r)
        h = std::fmod((g - b) / delta, 6.0);
    else if (maxC == g)
        h = (b - r) / delta + 2.0;
    else
        h = (r - g) / delta + 4.0;
    h *= 60.0;
    if (h < 0.0) h += 360.0;
    out.h = h;
    return out;
}

Rgb applyTint(const Rgb& color, double tint)
{
    if (tint == 0.0) return color;
    Hls hls = toHls(color);
    if (tint < 0.0)
        hls.l = hls.l * (1.0 + tint);
    else
        hls.l = hls.l * (1.0 - tint) + tint;
    hls.l = std::clamp(hls.l, 0.0, 1.0);
    return fromHls(hls);
}

std::optional<Rgb> legacyIndexedColor(unsigned index)
{
    if (index >= kLegacyPalette.size()) return std::nullopt;
    const std::uint32_t v = kLegacyPalette[index];
    return Rgb{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>((v >> 8) & 0xFF),
               static_cast<std::uint8_t>(v & 0xFF)};
}

bool RedCriteria::matches(const Rgb& color) const
{
    if (!exactColors.empty()) return std::find(exactColors.begin(), exactColors.end(), color) != exactColors.end();

    const Hsv hsv = toHsv(color);
    const double hueDistance = std::min(hsv.h, 360.0 - hsv.h);
    return hueDistance <= maxHueDeviation && hsv.s >= minSaturation && hsv.v >= minValue;
}

}    // namespace redclean
