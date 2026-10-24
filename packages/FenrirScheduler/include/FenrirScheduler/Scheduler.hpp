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
     * PostTick once per fixed step (zero or more times), then AfterTicks once (even with no tick), Update, PostUpdate
     * and Render.
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
        /**
         * @brief Once per frame, last: after PostUpdate and before the event queues swap (ADR-0019). It sees the poses
         * interpolated in AfterTicks and everything written in Update and PostUpdate, C# included. Its systems read
         * scene state and draw it; writing scene state here is unsupported. Every Render system is sequential, so it
         * runs on the main thread.
         */
        Render,
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

        /** @brief Adds systems that run in parallel, on worker threads (see AddSystem). */
        Scheduler& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);
        /** @brief Adds systems that run one after another, on the thread that runs the app (see AddSequentialSystem).
         */
        Scheduler& AddSequentialSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        /**
         * @brief Adds a system that runs in parallel with the phase's other parallel systems, on a worker thread, and
         * before its sequential ones. Not for systems that touch SDL or the GPU, which must run on the main thread:
         * add those with AddSequentialSystem. (The init phases run only systems added with AddSystem, one after another
         * on the app's thread.)
         */
        Scheduler& AddSystem(SchedulePriority priority, SystemFunc system);
        /**
         * @brief Adds a system that runs after the phase's parallel systems, one after another in the order added, on
         * the thread that runs the app: the main thread. Systems that touch SDL or the GPU (a window's event pump,
         * input dispatch, which may reach SDL through input routers) and every Render system must be added this way.
         */
        Scheduler& AddSequentialSystem(SchedulePriority priority, SystemFunc system);

        /** @brief Runs one init phase's systems (PreInit, Init or PostInit), in the order added. */
        void RunInitSystems(App& app, SchedulePriority priority);
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