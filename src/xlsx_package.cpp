/**
 * @file xlsx_package.cpp
 * @brief Реализация работы с пакетом .xlsx.
 */

#include "xlsx_package.hpp"

#include "xml_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace redclean {

namespace {

/// @brief Размер куска изменённой части для параллельного сжатия.
constexpr std::size_t kChunkSize = std::size_t{4} << 20;

/// @brief Оканчивается ли @p s на @p suffix.
bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

/**
 * @brief Папка части с завершающим @c '/'.
 * @param path Путь части.
 * @return Например, @c "xl/" для @c "xl/workbook.xml"; пусто для корня.
 */
std::string directoryOf(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

/**
 * @brief Путь части связей для заданной части.
 * @param partPath Путь части, например @c "xl/workbook.xml".
 * @return Например, @c "xl/_rels/workbook.xml.rels".
 */
std::string relsPathFor(const std::string& partPath)
{
    const auto slash = partPath.rfind('/');
    const std::string dir = slash == std::string::npos ? std::string{} : partPath.substr(0, slash + 1);
    const std::string file = slash == std::string::npos ? partPath : partPath.substr(slash + 1);
    return dir + "_rels/" + file + ".rels";
}

/**
 * @brief Разрешает атрибут @c Target связи в путь внутри архива.
 * @param baseDir Папка части-источника связи.
 * @param target Значение @c Target: относительное или абсолютное (с ведущим @c '/').
 * @return Нормализованный путь без @c "." и @c "..".
 */
std::string resolveTarget(const std::string& baseDir, std::string_view target)
{
    std::string joined = (!target.empty() && target.front() == '/') ? std::string(target.substr(1))
                                                                     : baseDir + std::string(target);
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (pos <= joined.size()) {
        const auto next = joined.find('/', pos);
        const std::string part = joined.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        }
        else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

/// @brief Связь из части @c .rels.
struct Relationship
{
    std::string id;        ///< Идентификатор (@c Id).
    std::string type;      ///< URI типа связи.
    std::string target;    ///< Цель связи.
};

/**
 * @brief Разбирает часть связей, пропуская внешние (@c TargetMode="External").
 * @param data Текст части @c .rels; пустой — связей нет.
 * @param what Описание для сообщения об ошибке.
 * @return Связи в порядке следования.
 * @throws std::runtime_error Если XML некорректен.
 */
std::vector<Relationship> parseRelationships(const std::string& data, const std::string& what)
{
    std::vector<Relationship> rels;
    if (data.empty()) return rels;
    pugi::xml_document doc;
    xml::load(doc, data, what);
    xml::forEachChild(doc.document_element(), "Relationship", [&](const pugi::xml_node& rel) {
        if (std::string_view(xml::attribute(rel, "TargetMode").value()) == "External") return;
        rels.push_back({xml::attribute(rel, "Id").value(), xml::attribute(rel, "Type").value(),
                        xml::attribute(rel, "Target").value()});
    });
    return rels;
}

/**
 * @brief Открывает архив с понятными сообщениями об ошибках.
 * @param path Путь к файлу в UTF-8.
 * @return Читатель архива.
 * @throws std::runtime_error Если файла нет или он не zip-архив.
 */
ZipReader openZip(const std::string& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), ec))
        throw std::runtime_error("Файл \"" + path + "\" не найден");
    try {
        return ZipReader(path);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Не удалось открыть \"" + path + "\" как xlsx: " + e.what());
    }
}

}    // namespace

XlsxPackage::XlsxPackage(const std::string& path) : m_zip(openZip(path))
{
    for (const auto& rel : parseRelationships(read("_rels/.rels"), "_rels/.rels"))
        if (endsWith(rel.type, "/officeDocument")) m_workbookPath = resolveTarget("", rel.target);
    if (m_workbookPath.empty() || !m_zip.contains(m_workbookPath))
        throw std::runtime_error("\"" + path + "\" не похож на книгу Excel: не найден workbook.xml");

    m_workbookRelsPath = relsPathFor(m_workbookPath);
    const std::string baseDir = directoryOf(m_workbookPath);
    std::unordered_map<std::string, Relationship> relById;
    for (auto& rel : parseRelationships(read(m_workbookRelsPath), m_workbookRelsPath)) {
        rel.target = resolveTarget(baseDir, rel.target);
        if (endsWith(rel.type, "/styles")) m_stylesPath = rel.target;
        if (endsWith(rel.type, "/theme")) m_themePath = rel.target;
        if (endsWith(rel.type, "/calcChain")) m_calcChainPath = rel.target;
        relById.emplace(rel.id, rel);
    }

    pugi::xml_document workbook;
    xml::load(workbook, read(m_workbookPath), m_workbookPath);
    xml::forEachChild(xml::child(workbook.document_element(), "sheets"), "sheet", [&](const pugi::xml_node& sheet) {
        const std::string name = xml::attribute(sheet, "name").value();
        const auto it = relById.find(xml::attribute(sheet, "id").value());
        if (it != relById.end() && endsWith(it->second.type, "/worksheet"))
            m_worksheets.push_back({name, it->second.target});
        else
            m_otherSheetNames.push_back(name);
    });
}

std::optional<SheetInfo> XlsxPackage::findWorksheet(const std::string& name) const
{
    for (const auto& sheet : m_worksheets)
        if (sheet.name == name) return sheet;
    for (const auto& other : m_otherSheetNames)
        if (other == name) throw std::runtime_error("Лист \"" + name + "\" не является обычным листом с ячейками");
    return std::nullopt;
}

std::string XlsxPackage::read(const std::string& partPath) const
{
    if (partPath.empty() || m_removed.count(partPath)) return {};
    if (const auto it = m_replaced.find(partPath); it != m_replaced.end()) return it->second;
    if (!m_zip.contains(partPath)) return {};
    return m_zip.read(partPath);
}

void XlsxPackage::write(const std::string& partPath, std::string data)
{
    m_removed.erase(partPath);
    m_replaced[partPath] = std::move(data);
}

bool XlsxPackage::removeCalcChain()
{
    if (m_calcChainPath.empty() || !m_zip.contains(m_calcChainPath)) return false;
    m_removed.insert(m_calcChainPath);
    m_replaced.erase(m_calcChainPath);

    pugi::xml_document rels;
    xml::load(rels, read(m_workbookRelsPath), m_workbookRelsPath);
    pugi::xml_node relsRoot = rels.document_element();
    for (pugi::xml_node rel = xml::child(relsRoot, "Relationship"); rel;) {
        const pugi::xml_node next = xml::nextSibling(rel, "Relationship");
        if (endsWith(xml::attribute(rel, "Type").value(), "/calcChain")) relsRoot.remove_child(rel);
        rel = next;
    }
    write(m_workbookRelsPath, xml::serialize(rels));

    const std::string contentTypesPath = "[Content_Types].xml";
    pugi::xml_document types;
    xml::load(types, read(contentTypesPath), contentTypesPath);
    pugi::xml_node typesRoot = types.document_element();
    const std::string partName = "/" + m_calcChainPath;
    for (pugi::xml_node node = xml::child(typesRoot, "Override"); node;) {
        const pugi::xml_node next = xml::nextSibling(node, "Override");
        if (partName == xml::attribute(node, "PartName").value()) typesRoot.remove_child(node);
        node = next;
    }
    write(contentTypesPath, xml::serialize(types));

    m_calcChainPath.clear();
    return true;
}

void XlsxPackage::saveAs(const std::string& path, ThreadPool& pool, int compressionLevel) const
{
    struct Chunk
    {
        const std::string* data;
        std::size_t begin;
        std::size_t end;
        bool last;
    };
    std::vector<const std::string*> parts;
    std::vector<std::vector<std::size_t>> chunksOfPart;
    std::vector<Chunk> chunks;
    for (const auto& [name, data] : m_replaced) {
        chunksOfPart.emplace_back();
        parts.push_back(&data);
        for (std::size_t begin = 0;;) {
            const std::size_t end = std::min(data.size(), begin + kChunkSize);
            chunksOfPart.back().push_back(chunks.size());
            chunks.push_back({&data, begin, end, end == data.size()});
            if (end == data.size()) break;
            begin = end;
        }
    }

    std::vector<std::string> deflated(chunks.size());
    std::vector<std::uint32_t> crcs(parts.size());
    parallelFor(pool, chunks.size() + parts.size(), 1, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            if (i < parts.size()) {
                crcs[i] = crc32(parts[i]->data(), parts[i]->size());
            }
            else {
                const Chunk& c = chunks[i - parts.size()];
                deflated[i - parts.size()] = deflateChunk(c.data->data() + c.begin, c.end - c.begin, compressionLevel, c.last);
            }
        }
    });

    std::map<std::string, CompressedEntry> compressed;
    std::size_t index = 0;
    for (const auto& [name, data] : m_replaced) {
        CompressedEntry entry;
        entry.uncompressedSize = data.size();
        entry.crc32 = crcs[index];
        for (const std::size_t c : chunksOfPart[index]) {
            entry.deflated += deflated[c];
            std::string().swap(deflated[c]);
        }
        compressed.emplace(name, std::move(entry));
        ++index;
    }

    ZipWriter writer;
    std::set<std::string> written;
    for (const auto& name : m_zip.entryNames()) {
        if (m_removed.count(name) || !written.insert(name).second) continue;
        if (const auto it = compressed.find(name); it != compressed.end())
            writer.addCompressed(name, it->second);
        else
            writer.copyFrom(m_zip, name);
    }
    for (const auto& [name, entry] : compressed)
        if (!written.count(name)) writer.addCompressed(name, entry);
    writer.finish(path);
}

}    // namespace redclean
