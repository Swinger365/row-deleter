/**
 * @file test_thread_pool.cpp
 * @brief Тесты пула потоков.
 */

#include "thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <stdexcept>

using namespace redclean;

TEST(ThreadPool, ReturnsResults)
{
    ThreadPool pool(3);
    EXPECT_EQ(pool.size(), 3u);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) futures.push_back(pool.submit([i] { return i * i; }));
    for (int i = 0; i < 100; ++i) EXPECT_EQ(futures[i].get(), i * i);
}

TEST(ThreadPool, DefaultSizeIsPositive)
{
    ThreadPool pool;
    EXPECT_GE(pool.size(), 1u);
}

/**
 * @test Четыре задачи одновременно находятся в работе на разных потоках.
 */
TEST(ThreadPool, UsesMultipleThreads)
{
    ThreadPool pool(4);
    std::mutex mutex;
    std::set<std::thread::id> ids;
    std::atomic<int> arrived{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 4; ++i)
        futures.push_back(pool.submit([&] {
            {
                std::lock_guard lock(mutex);
                ids.insert(std::this_thread::get_id());
            }
            ++arrived;
            while (arrived.load() < 4) std::this_thread::yield();
        }));
    waitAll(futures);
    EXPECT_EQ(ids.size(), 4u);
}

TEST(ThreadPool, ParallelForCoversEveryIndexOnce)
{
    ThreadPool pool(4);
    std::vector<std::atomic<int>> hits(10007);
    parallelFor(pool, hits.size(), 64, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) ++hits[i];
    });
    for (const auto& h : hits) ASSERT_EQ(h.load(), 1);
}

TEST(ThreadPool, ParallelForHandlesEmptyRange)
{
    ThreadPool pool(2);
    bool called = false;
    parallelFor(pool, 0, 10, [&](std::size_t, std::size_t) { called = true; });
    EXPECT_FALSE(called);
}

/**
 * @test Исключение задачи доходит до future, после чего пул остаётся рабочим.
 */
TEST(ThreadPool, ExceptionsPropagate)
{
    ThreadPool pool(2);
    EXPECT_THROW(parallelFor(pool, 100, 10,
                             [](std::size_t begin, std::size_t) {
                                 if (begin == 50) throw std::runtime_error("boom");
                             }),
                 std::runtime_error);
    EXPECT_EQ(pool.submit([] { return 42; }).get(), 42);
}
