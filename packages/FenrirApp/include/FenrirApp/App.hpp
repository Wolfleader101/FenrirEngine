#include <functional>
#include <initializer_list>
#include <map>

namespace Fenrir
{

    // bevy wraps systems in a class with extra methods such as "before", "after", "run_if", for now i wont add these
    // but i might in the future
    using SystemFunc = std::function<void()>;

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
        Last,
    };

    class Scheduler
    {
      public:
        Scheduler();

        Scheduler& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        Scheduler& AddSystem(SchedulePriority priority, SystemFunc system);

        void Init();
        void Run();

      private:
        std::map<SchedulePriority, std::vector<SystemFunc>> m_runOnceSystems;
        std::map<SchedulePriority, std::vector<SystemFunc>> m_updateSystems;

        bool IsRunOnceSystem(SchedulePriority priority);
    };

    class App
    {
      public:
        App();

        App& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        App& AddSystem(SchedulePriority priority, SystemFunc system);

        void Run();

      private:
        Scheduler m_scheduler;
        bool m_running = true;
    };

} // namespace Fenrir