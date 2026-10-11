/**
 * @file test_zip_archive.cpp
 * @brief Тесты чтения и записи zip-архивов.
 */

#include "thread_pool.hpp"
#include "zip_archive.hpp"

#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <fstream>

using namespace redclean;
using namespace testutil;

namespace {

/// @brief Текст размером в несколько кусков сжатия.
std::string bigText()
{
    std::string s;
    for (int i = 0; i < 100000; ++i) s += "<row r=\"" + std::to_string(i) + "\"><c><v>" + std::to_string(i * 7) + "</v></c></row>";
    return s;
}

}    // namespace

TEST(Zip, RoundTripPreservesNamesOrderAndData)
{
    TempDir dir;
    const auto path = dir.file("Книга с пробелами.xlsx").string();
    const std::string big = bigText();
    {
        ZipWriter writer;
        writer.add("b.xml", "второй");
        writer.add("a/empty.xml", "");
        writer.add("a/big.xml", big);
        writer.finish(path);
    }
    ZipReader reader(path);
    EXPECT_EQ(reader.entryNames(), (std::vector<std::string>{"b.xml", "a/empty.xml", "a/big.xml"}));
    EXPECT_EQ(reader.read("b.xml"), "второй");
    EXPECT_EQ(reader.read("a/empty.xml"), "");
    EXPECT_EQ(reader.read("a/big.xml"), big);
    EXPECT_TRUE(reader.contains("a/big.xml"));
    EXPECT_FALSE(reader.contains("nope"));
    EXPECT_THROW(reader.read("nope"), std::runtime_error);
}

TEST(Zip, CopyAndPrecompressedEntries)
{
    TempDir dir;
    const auto src = dir.file("src.zip").string();
    const auto dst = dir.file("dst.zip").string();
    {
        ZipWriter writer;
        writer.add("keep.xml", bigText());
        writer.add("replace.xml", "old");
        writer.finish(src);
    }
    ZipReader source(src);
    {
        ZipWriter writer;
        writer.copyFrom(source, "keep.xml");
        writer.addCompressed("replace.xml", compressEntry("new content", 6));
        writer.finish(dst);
    }
    ZipReader result(dst);
    EXPECT_EQ(result.read("keep.xml"), source.read("keep.xml"));
    EXPECT_EQ(result.read("replace.xml"), "new content");
}

/**
 * @test Метаданные сжатой записи, включая CRC-32 строки "hello".
 */
TEST(Zip, CompressedEntryMetadata)
{
    const CompressedEntry e = compressEntry("hello", 6);
    EXPECT_EQ(e.uncompressedSize, 5u);
    EXPECT_EQ(e.crc32, 0x3610A686u);
    EXPECT_FALSE(e.deflated.empty());
}

TEST(Zip, ChunkedDeflateConcatenatesIntoValidStream)
{
    const std::string data = bigText();
    CompressedEntry entry;
    entry.uncompressedSize = data.size();
    entry.crc32 = crc32(data.data(), data.size());
    const std::size_t chunk = data.size() / 5 + 1;
    for (std::size_t begin = 0; begin < data.size(); begin += chunk) {
        const std::size_t size = std::min(chunk, data.size() - begin);
        entry.deflated += deflateChunk(data.data() + begin, size, 6, begin + size == data.size());
    }

    TempDir dir;
    const auto path = dir.file("chunked.zip").string();
    ZipWriter writer;
    writer.addCompressed("data.xml", entry);
    writer.finish(path);
    EXPECT_EQ(ZipReader(path).read("data.xml"), data);
}

TEST(Zip, ConcurrentReads)
{
    TempDir dir;
    const auto path = dir.file("many.zip").string();
    std::vector<std::string> contents;
    {
        ZipWriter writer;
        for (int i = 0; i < 16; ++i) {
            contents.push_back(bigText() + std::to_string(i));
            writer.add("part" + std::to_string(i) + ".xml", contents.back());
        }
        writer.finish(path);
    }
    ZipReader reader(path);
    ThreadPool pool(8);
    std::vector<std::future<bool>> futures;
    for (int round = 0; round < 4; ++round)
        for (int i = 0; i < 16; ++i)
            futures.push_back(pool.submit([&, i] { return reader.read("part" + std::to_string(i) + ".xml") == contents[i]; }));
    for (auto& f : futures) EXPECT_TRUE(f.get());
}

TEST(Zip, FinishOverwritesExistingFile)
{
    TempDir dir;
    const auto path = dir.file("out.zip").string();
    for (const char* text : {"first", "second"}) {
        ZipWriter writer;
        writer.add("x", text);
        writer.finish(path);
    }
    EXPECT_EQ(ZipReader(path).read("x"), "second");
}

TEST(Zip, InvalidInput)
{
    TempDir dir;
    EXPECT_THROW(ZipReader(dir.file("missing.zip").string()), std::runtime_error);
    std::ofstream(dir.file("junk.zip")) << "definitely not a zip archive";
    EXPECT_THROW(ZipReader(dir.file("junk.zip").string()), std::runtime_error);
}
