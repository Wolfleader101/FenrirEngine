#pragma once

#include <functional>
#include <initializer_list>
#include <map>
#include <vector>

#include "ThreadPool.hpp"

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
    /**
     * @brief When in the app's life a system runs. A frame runs PreUpdate, BeforeTicks once, then PreTick, Tick and
     * PostTick once per fixed step (zero or more times), then AfterTicks once (even with no tick), Update and
     * PostUpdate.
     */
    enum class SchedulePriority
    {
        PreInit,
        Init,
        PostInit,
        PreUpdate,
        /** @brief Once per frame, before its fixed steps (even when it has none). */
        BeforeTicks,
        /** @brief Once per fixed step, before Tick. */
        PreTick,
        Tick,
        /** @brief Once per fixed step, after Tick. */
        PostTick,
        /** @brief Once per frame, after its fixed steps (even when it has none). */
        AfterTicks,
        Update,
        PostUpdate,
        // CreateScene,
        // LastUpdate,
        Exit
    };

    /**
     * @brief The events one system sent while the scheduler ran it in parallel with others. They are held until the
     * whole batch has run, then sent in the order the systems were registered, so parallel sends arrive in the same
     * order every run. Sequential systems have none and send directly.
     */
    class SendBuffer
    {
      public:
        /** @brief The buffer of the parallel system running on this thread, or null outside a parallel system. */
        [[nodiscard]] static SendBuffer* OfThisThread() noexcept;

        /** @brief Holds a send until the system's batch has run. */
        void Add(std::function<void()> send);

        /** @brief Makes the held sends, in the order they were added, and empties the buffer. */
        void Flush();

      private:
        std::vector<std::function<void()>> m_sends;
    };

    class Scheduler
    {
      public:
        /** @brief A scheduler whose parallel systems run on one worker thread per hardware thread. */
        Scheduler();

        /** @brief A scheduler whose parallel systems run on `workerThreads` worker threads (at least one). */
        explicit Scheduler(std::size_t workerThreads);

        Scheduler& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);
        Scheduler& AddSequentialSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        Scheduler& AddSystem(SchedulePriority priority, SystemFunc system);
        Scheduler& AddSequentialSystem(SchedulePriority priority, SystemFunc system);

        void Init(App& app);
        void RunSystems(App& app, SchedulePriority priority);

        /** @brief The worker threads parallel systems run on, which other engine work may share. */
        [[nodiscard]] ThreadPool& GetThreadPool() noexcept
        {
            return m_threadPool;
        }

      private:
        std::map<SchedulePriority, std::vector<SystemFunc>> m_runOnceSystems;
        std::map<SchedulePriority, std::vector<SystemFunc>> m_systems;
        std::map<SchedulePriority, std::vector<SystemFunc>> m_sequentialSystems;
        ThreadPool m_threadPool;

        bool IsRunOnceSystem(SchedulePriority priority);
        void RunSequentialSystems(App& app, SchedulePriority priority);
    };
} // namespace Fenrir