/**
 * @file xml_utils.hpp
 * @brief Вспомогательные функции поверх pugixml, не зависящие от префиксов пространств имён.
 */

#pragma once

#include <pugixml.hpp>

#include <string>
#include <string_view>

/// @brief Работа с XML-частями пакета.
namespace redclean::xml {

/**
 * @brief Отбрасывает префикс пространства имён.
 * @param qualifiedName Полное имя, например @c "x:row".
 * @return Локальное имя, например @c "row".
 */
std::string_view localName(const char* qualifiedName);

/**
 * @brief Локальное имя узла без префикса.
 * @param node Узел XML.
 * @return Имя узла без префикса пространства имён.
 */
inline std::string_view localName(const pugi::xml_node& node) { return localName(node.name()); }

/**
 * @brief Первый дочерний элемент с заданным локальным именем.
 * @details Префикс игнорируется: @c &lt;row&gt; и @c &lt;x:row&gt; считаются одинаковыми.
 * @param parent Родительский узел.
 * @param name Локальное имя искомого элемента.
 * @return Найденный элемент или пустой узел.
 */
pugi::xml_node child(const pugi::xml_node& parent, std::string_view name);

/**
 * @brief Следующий соседний элемент с заданным локальным именем.
 * @param node Текущий элемент.
 * @param name Локальное имя искомого элемента.
 * @return Найденный элемент или пустой узел.
 */
pugi::xml_node nextSibling(const pugi::xml_node& node, std::string_view name);

/**
 * @brief Атрибут по имени с откатом на локальное имя.
 * @details Сначала ищется точное совпадение, затем атрибут с префиксом и тем же
 *          локальным именем (так @c "id" находит @c "r:id").
 * @param node Элемент.
 * @param name Имя атрибута.
 * @return Найденный атрибут или пустой атрибут.
 */
pugi::xml_attribute attribute(const pugi::xml_node& node, std::string_view name);

/**
 * @brief Вызывает @p fn для каждого дочернего элемента с локальным именем @p name.
 * @tparam Fn Вызываемый объект с сигнатурой @c void(const pugi::xml_node&).
 * @param parent Родительский узел.
 * @param name Локальное имя элементов.
 * @param fn Обработчик.
 */
template <typename Fn>
void forEachChild(const pugi::xml_node& parent, std::string_view name, Fn&& fn)
{
    for (pugi::xml_node node = child(parent, name); node; node = nextSibling(node, name)) fn(node);
}

/**
 * @brief Флаги разбора, сохраняющие документ максимально близким к исходному.
 * @details Сохраняются объявление XML и текстовые узлы из одних пробелов
 *          (важно для @c &lt;t xml:space="preserve"&gt;).
 */
constexpr unsigned kParseFlags = pugi::parse_default | pugi::parse_declaration | pugi::parse_ws_pcdata;

/**
 * @brief Разбирает XML в документ.
 * @param doc Документ-приёмник.
 * @param data Текст XML.
 * @param what Описание части для сообщения об ошибке.
 * @throws std::runtime_error Если XML некорректен.
 */
void load(pugi::xml_document& doc, const std::string& data, const std::string& what);

/**
 * @brief Сериализует документ без переформатирования, в UTF-8.
 * @param doc Документ.
 * @return Текст XML.
 */
std::string serialize(const pugi::xml_document& doc);

}    // namespace redclean::xml
