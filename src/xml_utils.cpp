/**
 * @file xml_utils.cpp
 * @brief Реализация вспомогательных функций для XML.
 */

#include "xml_utils.hpp"

#include <sstream>
#include <stdexcept>

namespace redclean::xml {

std::string_view localName(const char* qualifiedName)
{
    std::string_view name(qualifiedName);
    const auto colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

pugi::xml_node child(const pugi::xml_node& parent, std::string_view name)
{
    for (pugi::xml_node node = parent.first_child(); node; node = node.next_sibling())
        if (node.type() == pugi::node_element && localName(node) == name) return node;
    return {};
}

pugi::xml_node nextSibling(const pugi::xml_node& node, std::string_view name)
{
    for (pugi::xml_node next = node.next_sibling(); next; next = next.next_sibling())
        if (next.type() == pugi::node_element && localName(next) == name) return next;
    return {};
}

pugi::xml_attribute attribute(const pugi::xml_node& node, std::string_view name)
{
    pugi::xml_attribute fallback;
    for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
        if (name == attr.name()) return attr;
        if (!fallback && localName(attr.name()) == name) fallback = attr;
    }
    return fallback;
}

void load(pugi::xml_document& doc, const std::string& data, const std::string& what)
{
    const pugi::xml_parse_result result = doc.load_buffer(data.data(), data.size(), kParseFlags);
    if (!result)
        throw std::runtime_error("Не удалось разобрать XML (" + what + "): " + result.description() + " на смещении " +
                                 std::to_string(result.offset));
}

std::string serialize(const pugi::xml_document& doc)
{
    std::ostringstream out;
    doc.save(out, "", pugi::format_raw, pugi::encoding_utf8);
    return std::move(out).str();
}

}    // namespace redclean::xml
