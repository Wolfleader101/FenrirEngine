#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace Fenrir
{
    /**
     * @brief A thread pool class that can be used to run functions on multiple threads
     * @author Threadpool class comes from: https://github.com/progschj/ThreadPool
     *
     */
    class ThreadPool
    {
      public:
        /**
         * @brief Construct a new Thread Pool object
         *
         * @param threads The number of threads to create
         */
        ThreadPool(size_t threads);

        /**
         * @brief Destroy the Thread Pool object
         *
         */
        ~ThreadPool();

        /**
         * @brief Enqueue a function to be run on a thread
         *
         * @tparam F the function type
         * @tparam Args the arguments to the function
         * @param f the function to run
         * @param args the arguments to the function
         * @return auto the return type of the function
         */
        template <class F, class... Args>
        auto Enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

      private:
        std::vector<std::thread> m_workers;

        std::queue<std::function<void()>> m_tasks;

        std::mutex m_queueMutex;

        std::condition_variable m_condition;

        bool m_stop;
    };

    template <class F, class... Args>
    auto ThreadPool::Enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);

            if (m_stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();
        return res;
    }

} // namespace Fenrir