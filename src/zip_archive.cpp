/**
 * @file zip_archive.cpp
 * @brief Реализация zip-слоя на miniz.
 */

#include "zip_archive.hpp"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

namespace redclean {

namespace fs = std::filesystem;

namespace {

/// @brief Текст последней ошибки miniz для архива.
std::string zipError(const mz_zip_archive& zip) { return mz_zip_get_error_string(zip.m_last_error); }

/// @brief Освобождает память, выделенную miniz.
struct FreeDeleter
{
    /// @brief Освобождает @p p через @c mz_free.
    void operator()(void* p) const { mz_free(p); }
};

/**
 * @brief Открывает читатель miniz поверх буфера в памяти.
 * @param zip Структура архива; обнуляется и инициализируется.
 * @param buffer Содержимое zip-файла.
 * @throws std::runtime_error Если буфер не является zip-архивом.
 */
void initReader(mz_zip_archive& zip, const std::vector<char>& buffer)
{
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_mem(&zip, buffer.data(), buffer.size(), 0))
        throw std::runtime_error("повреждённый zip-архив: " + zipError(zip));
}

}    // namespace

std::uint32_t crc32(const char* data, std::size_t size)
{
    return static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data), size));
}

std::string deflateChunk(const char* data, std::size_t size, int level, bool last)
{
    const mz_uint flags = tdefl_create_comp_flags_from_zip_params(level, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);
    std::unique_ptr<tdefl_compressor> compressor(new tdefl_compressor);

    std::string out;
    out.reserve(size / 4 + 64);
    const tdefl_put_buf_func_ptr append = [](const void* buf, int len, void* user) -> mz_bool {
        static_cast<std::string*>(user)->append(static_cast<const char*>(buf), static_cast<std::size_t>(len));
        return MZ_TRUE;
    };
    if (tdefl_init(compressor.get(), append, &out, static_cast<int>(flags)) != TDEFL_STATUS_OKAY)
        throw std::runtime_error("ошибка инициализации deflate");
    const tdefl_status expected = last ? TDEFL_STATUS_DONE : TDEFL_STATUS_OKAY;
    if (tdefl_compress_buffer(compressor.get(), data, size, last ? TDEFL_FINISH : TDEFL_FULL_FLUSH) != expected)
        throw std::runtime_error("ошибка сжатия deflate");
    return out;
}

CompressedEntry compressEntry(const std::string& data, int level)
{
    CompressedEntry entry;
    entry.uncompressedSize = data.size();
    entry.crc32 = crc32(data.data(), data.size());
    entry.deflated = deflateChunk(data.data(), data.size(), level, true);
    return entry;
}

/// @brief Состояние читателя miniz главного потока.
struct ZipReader::Impl
{
    mz_zip_archive zip{};    ///< Читатель для построения списка записей и копирования.
    ~Impl() { mz_zip_reader_end(&zip); }
};

ZipReader::ZipReader(const std::string& path) : m_impl(std::make_unique<Impl>())
{
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) throw std::runtime_error("не удалось открыть файл \"" + path + "\"");
    in.seekg(0, std::ios::end);
    m_buffer.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(m_buffer.data(), static_cast<std::streamsize>(m_buffer.size()));
    if (!in) throw std::runtime_error("ошибка чтения \"" + path + "\"");

    initReader(m_impl->zip, m_buffer);
    const mz_uint count = mz_zip_reader_get_num_files(&m_impl->zip);
    m_names.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&m_impl->zip, i, &stat)) throw std::runtime_error("повреждённый zip-архив");
        m_names.emplace_back(stat.m_filename);
    }
}

ZipReader::~ZipReader() = default;

bool ZipReader::contains(const std::string& name) const
{
    for (const auto& n : m_names)
        if (n == name) return true;
    return false;
}

std::string ZipReader::read(const std::string& name) const
{
    mz_zip_archive zip;
    initReader(zip, m_buffer);
    struct Guard
    {
        mz_zip_archive& z;
        ~Guard() { mz_zip_reader_end(&z); }
    } guard{zip};

    const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    if (index < 0) throw std::runtime_error("в архиве нет части \"" + name + "\"");
    std::size_t size = 0;
    std::unique_ptr<void, FreeDeleter> data(mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0));
    if (!data) {
        if (zip.m_last_error == MZ_ZIP_NO_ERROR) return {};
        throw std::runtime_error("не удалось распаковать \"" + name + "\": " + zipError(zip));
    }
    return std::string(static_cast<const char*>(data.get()), size);
}

/// @brief Состояние писателя miniz.
struct ZipWriter::Impl
{
    mz_zip_archive zip{};    ///< Писатель miniz.
    bool open = false;       ///< Писатель инициализирован и ещё не закрыт.

    ~Impl()
    {
        if (open) mz_zip_writer_end(&zip);
    }

    /**
     * @brief Бросает исключение с текстом ошибки miniz, если операция не удалась.
     * @param ok Результат операции.
     * @param what Описание операции.
     */
    void check(bool ok, const std::string& what)
    {
        if (!ok) throw std::runtime_error(what + ": " + zipError(zip));
    }
};

ZipWriter::ZipWriter() : m_impl(std::make_unique<Impl>())
{
    mz_zip_zero_struct(&m_impl->zip);
    m_impl->check(mz_zip_writer_init_heap(&m_impl->zip, 0, 0), "не удалось создать zip-архив");
    m_impl->open = true;
}

ZipWriter::~ZipWriter() = default;

void ZipWriter::add(const std::string& name, const std::string& data, int level)
{
    if (data.empty()) {
        m_impl->check(mz_zip_writer_add_mem(&m_impl->zip, name.c_str(), nullptr, 0, 0), "ошибка записи " + name);
        return;
    }
    addCompressed(name, compressEntry(data, level));
}

void ZipWriter::addCompressed(const std::string& name, const CompressedEntry& entry)
{
    if (entry.uncompressedSize == 0) {
        m_impl->check(mz_zip_writer_add_mem(&m_impl->zip, name.c_str(), nullptr, 0, 0), "ошибка записи " + name);
        return;
    }
    m_impl->check(mz_zip_writer_add_mem_ex(&m_impl->zip, name.c_str(), entry.deflated.data(), entry.deflated.size(), nullptr, 0,
                                           MZ_DEFAULT_LEVEL | MZ_ZIP_FLAG_COMPRESSED_DATA, entry.uncompressedSize, entry.crc32),
                  "ошибка записи " + name);
}

void ZipWriter::copyFrom(const ZipReader& source, const std::string& name)
{
    mz_zip_archive& src = source.m_impl->zip;
    const int index = mz_zip_reader_locate_file(&src, name.c_str(), nullptr, 0);
    if (index < 0) throw std::runtime_error("в архиве нет части \"" + name + "\"");
    m_impl->check(mz_zip_writer_add_from_zip_reader(&m_impl->zip, &src, static_cast<mz_uint>(index)), "ошибка копирования " + name);
}

void ZipWriter::finish(const std::string& path)
{
    void* buffer = nullptr;
    std::size_t size = 0;
    m_impl->check(mz_zip_writer_finalize_heap_archive(&m_impl->zip, &buffer, &size), "ошибка финализации архива");
    std::unique_ptr<void, FreeDeleter> data(buffer);

    const fs::path target = fs::u8path(path);
    fs::path temp = target;
    temp += ".tmp" + std::to_string(std::random_device{}());
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data.get()), static_cast<std::streamsize>(size));
        out.close();
        if (!out) {
            std::error_code ec;
            fs::remove(temp, ec);
            throw std::runtime_error("не удалось записать \"" + path + "\"");
        }
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(temp, target, ec);
    }
    if (ec) {
        fs::remove(temp, ec);
        throw std::runtime_error("не удалось сохранить \"" + path + "\": " + ec.message());
    }
}

}    // namespace redclean
