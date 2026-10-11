/**
 * @file zip_archive.hpp
 * @brief Чтение и запись zip-архивов (miniz) с поддержкой параллельного сжатия.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace redclean {

/// @brief Данные, уже сжатые raw-deflate (без zlib-заголовка), с CRC-32 и исходным размером.
struct CompressedEntry
{
    std::string deflated;               ///< Сжатый поток.
    std::uint32_t crc32 = 0;            ///< CRC-32 несжатых данных.
    std::uint64_t uncompressedSize = 0; ///< Размер несжатых данных.
};

/**
 * @brief Сжимает данные целиком.
 * @details Не зависит от архива; можно вызывать из любого потока.
 * @param data Несжатые данные.
 * @param level Уровень deflate 1..9.
 * @return Сжатая запись.
 */
CompressedEntry compressEntry(const std::string& data, int level);

/**
 * @brief Сжимает фрагмент raw-deflate потока.
 * @details Фрагменты, сжатые с @p last = @c false (сброс до границы байта без признака
 *          конца потока), и последний фрагмент с @p last = @c true можно просто склеить —
 *          получится корректный поток. Так одну большую часть можно сжимать параллельно.
 *          Потокобезопасна.
 * @param data Начало фрагмента.
 * @param size Размер фрагмента.
 * @param level Уровень deflate 1..9.
 * @param last Последний ли это фрагмент потока.
 * @return Сжатый фрагмент.
 */
std::string deflateChunk(const char* data, std::size_t size, int level, bool last);

/**
 * @brief CRC-32 (как в zip).
 * @param data Данные.
 * @param size Размер.
 * @return Контрольная сумма.
 */
std::uint32_t crc32(const char* data, std::size_t size);

/**
 * @brief Zip-архив только для чтения, целиком загруженный в память.
 * @details @ref read потокобезопасен: каждый вызов создаёт собственный читатель miniz
 *          поверх общего неизменяемого буфера.
 */
class ZipReader
{
public:
    /**
     * @brief Загружает архив.
     * @param path Путь в UTF-8.
     * @throws std::runtime_error Если файл не читается или не является zip-архивом.
     */
    explicit ZipReader(const std::string& path);
    ~ZipReader();

    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    /// @brief Имена записей в порядке их следования в архиве.
    const std::vector<std::string>& entryNames() const { return m_names; }

    /**
     * @brief Есть ли запись с таким именем.
     * @param name Имя записи.
     * @return @c true, если запись есть.
     */
    bool contains(const std::string& name) const;

    /**
     * @brief Распаковывает запись. Потокобезопасен.
     * @param name Имя записи.
     * @return Содержимое записи.
     * @throws std::runtime_error Если записи нет или она повреждена.
     */
    std::string read(const std::string& name) const;

private:
    friend class ZipWriter;
    struct Impl;
    std::vector<char> m_buffer;
    std::vector<std::string> m_names;
    std::unique_ptr<Impl> m_impl;    ///< Читатель главного потока; используется при копировании записей.
};

/**
 * @brief Собирает новый архив в памяти и сохраняет его в файл.
 * @details Не потокобезопасен; сжатие стоит выполнять заранее через @ref compressEntry
 *          или @ref deflateChunk в рабочих потоках и добавлять результат через @ref addCompressed.
 */
class ZipWriter
{
public:
    /// @brief Создаёт пустой архив.
    ZipWriter();
    ~ZipWriter();

    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    /**
     * @brief Сжимает и добавляет запись.
     * @param name Имя записи.
     * @param data Содержимое.
     * @param level Уровень deflate 1..9.
     */
    void add(const std::string& name, const std::string& data, int level = 6);

    /**
     * @brief Добавляет заранее сжатую запись без повторного сжатия.
     * @param name Имя записи.
     * @param entry Сжатые данные.
     */
    void addCompressed(const std::string& name, const CompressedEntry& entry);

    /**
     * @brief Копирует запись из другого архива как есть, без распаковки и повторного сжатия.
     * @param source Исходный архив.
     * @param name Имя записи.
     * @throws std::runtime_error Если записи нет.
     */
    void copyFrom(const ZipReader& source, const std::string& name);

    /**
     * @brief Завершает архив и сохраняет его.
     * @details Сначала пишется временный файл рядом с @p path, затем он переименовывается,
     *          поэтому при ошибке исходный файл не повреждается (в том числе при записи поверх входного).
     * @param path Путь в UTF-8.
     * @throws std::runtime_error При ошибке записи.
     */
    void finish(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}    // namespace redclean
