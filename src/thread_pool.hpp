/**
 * @file thread_pool.hpp
 * @brief Пул потоков и параллельные алгоритмы поверх него.
 */

#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace redclean {

/**
 * @brief Пул потоков фиксированного размера с общей очередью задач.
 * @details Задачи не должны блокироваться в ожидании других задач того же пула —
 *          иначе при занятых потоках возможна взаимная блокировка. Деструктор
 *          дожидается выполнения всех поставленных задач.
 */
class ThreadPool
{
public:
    /**
     * @brief Запускает рабочие потоки.
     * @param threadCount Число потоков; 0 — @ref defaultThreadCount().
     */
    explicit ThreadPool(std::size_t threadCount = 0)
    {
        if (threadCount == 0) threadCount = defaultThreadCount();
        m_workers.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) m_workers.emplace_back([this] { workerLoop(); });
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// @brief Дожидается всех задач и останавливает потоки.
    ~ThreadPool()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_cv.notify_all();
        for (auto& worker : m_workers) worker.join();
    }

    /// @brief Число аппаратных потоков (не меньше 1).
    static std::size_t defaultThreadCount() { return std::max<std::size_t>(1, std::thread::hardware_concurrency()); }

    /// @brief Число рабочих потоков.
    std::size_t size() const { return m_workers.size(); }

    /**
     * @brief Ставит задачу в очередь.
     * @tparam Fn Вызываемый объект без аргументов.
     * @param fn Задача.
     * @return @c std::future с результатом; исключение задачи пробрасывается из @c get().
     * @throws std::runtime_error Если пул уже останавливается.
     */
    template <typename Fn>
    auto submit(Fn&& fn) -> std::future<std::invoke_result_t<std::decay_t<Fn>>>
    {
        using Result = std::invoke_result_t<std::decay_t<Fn>>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard lock(m_mutex);
            if (m_stopping) throw std::runtime_error("ThreadPool: submit после остановки");
            m_tasks.emplace([task] { (*task)(); });
        }
        m_cv.notify_one();
        return future;
    }

private:
    /// @brief Цикл рабочего потока: берёт задачи из очереди, пока пул не остановлен.
    void workerLoop()
    {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_stopping && m_tasks.empty()) return;
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            task();
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopping = false;
};

/**
 * @brief Дожидается всех @p futures, затем пробрасывает первое исключение, если оно было.
 * @details Сначала ждёт все задачи, чтобы ни одна не продолжала работать со
 *          ссылками на уже уничтоженные данные вызывающего кода.
 * @tparam T Тип результата.
 * @param futures Ожидаемые результаты.
 */
template <typename T>
void waitAll(std::vector<std::future<T>>& futures)
{
    for (auto& f : futures) f.wait();
    for (auto& f : futures) f.get();
}

/**
 * @brief Параллельно обрабатывает диапазон [0, @p count) кусками.
 * @tparam Fn Вызываемый объект @c void(std::size_t begin, std::size_t end).
 * @param pool Пул потоков.
 * @param count Размер диапазона.
 * @param grain Максимальный размер куска (не меньше 1).
 * @param fn Обработчик куска [begin, end).
 * @throws Любое исключение из @p fn (первое по порядку кусков).
 */
template <typename Fn>
void parallelFor(ThreadPool& pool, std::size_t count, std::size_t grain, Fn&& fn)
{
    if (count == 0) return;
    grain = std::max<std::size_t>(1, grain);
    std::vector<std::future<void>> futures;
    futures.reserve((count + grain - 1) / grain);
    for (std::size_t begin = 0; begin < count; begin += grain) {
        const std::size_t end = std::min(count, begin + grain);
        futures.push_back(pool.submit([&fn, begin, end] { fn(begin, end); }));
    }
    waitAll(futures);
}

}    // namespace redclean
