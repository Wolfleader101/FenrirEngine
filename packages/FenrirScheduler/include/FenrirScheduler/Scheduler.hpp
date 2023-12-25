#pragma once

#include <functional>
#include <initializer_list>
#include <map>

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>

namespace Fenrir
{
    class App;
    // bevy wraps systems in a class with extra methods such as "before", "after", "run_if", for now i wont add these
    // but i might in the future
    using SystemFunc = std::function<void(App&)>;

    // systems should be able to be added to the app
    // a system should just be a function that is called at a certain point in the app's lifecycle
    // when adding a system it should also have a priority, and what functions it should hook into

    // schedule should just be an enum
    enum class SchedulePriority
    {
        PreInit,
        Init,
        PostInit,
        PreUpdate,
        Tick,
        Update,
        PostUpdate,
        // CreateScene,
        // LastUpdate,
        Exit
    };

    class ThreadPool
    {
      public:
        ThreadPool(size_t threads) : m_stop(false)
        {
            for (size_t i = 0; i < threads; ++i)
                m_workers.emplace_back([this] {
                    for (;;)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->m_queueMutex);
                            this->m_condition.wait(lock, [this] { return this->m_stop || !this->m_tasks.empty(); });
                            if (this->m_stop && this->m_tasks.empty())
                                return;
                            task = std::move(this->m_tasks.front());
                            this->m_tasks.pop();
                        }

                        task();
                    }
                });
        }
        ~ThreadPool()
        {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_stop = true;
            }
            m_condition.notify_all();
            for (std::thread& worker : m_workers)
                worker.join();
        }

        template <class F, class... Args>
        auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
        {
            using return_type = typename std::result_of<F(Args...)>::type;

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

      private:
        std::vector<std::thread> m_workers;

        std::queue<std::function<void()>> m_tasks;

        std::mutex m_queueMutex;

        std::condition_variable m_condition;

        bool m_stop;
    };

    class Scheduler
    {
      public:
        Scheduler();

        Scheduler& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);
        Scheduler& AddSequentialSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        Scheduler& AddSystem(SchedulePriority priority, SystemFunc system);
        Scheduler& AddSequentialSystem(SchedulePriority priority, SystemFunc system);

        void Init(App& app);
        void RunSystems(App& app, SchedulePriority priority);

      private:
        std::map<SchedulePriority, std::vector<SystemFunc>> m_runOnceSystems;
        std::map<SchedulePriority, std::vector<SystemFunc>> m_systems;
        std::map<SchedulePriority, std::vector<SystemFunc>> m_sequentialSystems;
        ThreadPool m_threadPool;

        bool IsRunOnceSystem(SchedulePriority priority);
        void RunSequentialSystems(App& app, SchedulePriority priority);
    };
} // namespace Fenrir