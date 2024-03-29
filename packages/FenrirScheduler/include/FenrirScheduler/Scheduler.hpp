#pragma once

#include <functional>
#include <initializer_list>
#include <map>

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