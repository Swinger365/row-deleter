/**
 * @file color.hpp
 * @brief Цвета: разбор, преобразования RGB/HSV/HLS, палитра Excel и критерий «красного».
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @brief Корневое пространство имён проекта.
namespace redclean {

/// @brief Цвет в RGB, по байту на канал.
struct Rgb
{
    std::uint8_t r = 0;    ///< Красный канал.
    std::uint8_t g = 0;    ///< Зелёный канал.
    std::uint8_t b = 0;    ///< Синий канал.

    /// @brief Покомпонентное сравнение.
    friend bool operator==(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
    /// @brief Покомпонентное сравнение.
    friend bool operator!=(const Rgb& a, const Rgb& b) { return !(a == b); }
};

/// @brief Цвет в модели HSV.
struct Hsv
{
    double h = 0.0;    ///< Оттенок, градусы в диапазоне [0, 360).
    double s = 0.0;    ///< Насыщенность, [0, 1].
    double v = 0.0;    ///< Яркость, [0, 1].
};

/**
 * @brief Разбирает цвет из шестнадцатеричной записи.
 * @param text @c "RRGGBB" или @c "AARRGGBB", допускается ведущий @c '#'.
 *             Альфа-канал игнорируется, как и в Excel.
 * @return Цвет или @c std::nullopt при неверном формате.
 */
std::optional<Rgb> parseHexColor(std::string_view text);

/**
 * @brief Форматирует цвет как @c "RRGGBB" (заглавные буквы).
 * @param color Цвет.
 * @return Шестнадцатеричная запись.
 */
std::string toHex(const Rgb& color);

/**
 * @brief Переводит цвет в HSV.
 * @param color Цвет в RGB.
 * @return Цвет в HSV.
 */
Hsv toHsv(const Rgb& color);

/**
 * @brief Применяет оттенок (атрибут @c tint из OOXML).
 * @details Яркость в модели HLS: при @p tint < 0 она уменьшается в (1 + tint) раз,
 *          при @p tint > 0 смещается к белому: L' = L·(1 − tint) + tint.
 * @param color Исходный цвет.
 * @param tint Значение в диапазоне [−1, 1].
 * @return Изменённый цвет.
 */
Rgb applyTint(const Rgb& color, double tint);

/**
 * @brief Цвет из устаревшей 64-цветной палитры Excel (атрибут @c indexed).
 * @param index Индекс палитры.
 * @return Цвет или @c std::nullopt для индексов ≥ 64 (64 и 65 — системные цвета).
 */
std::optional<Rgb> legacyIndexedColor(unsigned index);

/**
 * @brief Критерий, по которому цвет считается красным.
 * @details Если задан список @ref exactColors, проверяется только точное совпадение.
 *          Иначе используется эвристика по HSV: оттенок не дальше @ref maxHueDeviation
 *          от 0°, насыщенность и яркость не ниже порогов.
 */
struct RedCriteria
{
    std::vector<Rgb> exactColors;      ///< Точный список «красных» цветов; пустой — эвристика.
    double maxHueDeviation = 20.0;     ///< Допустимое отклонение оттенка от 0°, градусы.
    double minSaturation = 0.5;        ///< Минимальная насыщенность.
    double minValue = 0.5;             ///< Минимальная яркость.

    /**
     * @brief Проверяет цвет.
     * @param color Цвет.
     * @return @c true, если цвет считается красным.
     */
    bool matches(const Rgb& color) const;
};

}    // namespace redclean
