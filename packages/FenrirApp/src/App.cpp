#include "FenrirApp/App.hpp"

namespace Fenrir
{
    App::App()
    {
    }

    App& App::AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems)
    {
        m_scheduler.AddSystems(priority, systems);
        return *this;
    }

    App& App::AddSystem(SchedulePriority priority, SystemFunc system)
    {
        m_scheduler.AddSystem(priority, system);
        return *this;
    }

    void App::Run()
    {
        // TODO eventually make this a loop
        m_scheduler.Run();
    }

    Scheduler::Scheduler()
    {
    }

    Scheduler& Scheduler::AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems)
    {
        // for each system, add it to the map
        for (auto system : systems)
        {
            m_systems[priority].push_back(system);
        }

        return *this;
    }

    Scheduler& Scheduler::AddSystem(SchedulePriority priority, SystemFunc system)
    {
        // add the system to the map
        m_systems[priority].push_back(system);
        return *this;
    }

    void Scheduler::Run()
    {
        // for each priority, run each system in order of their insertion
        for (auto& [priority, systems] : m_systems)
        {
            for (auto& system : systems)
            {
                system(); // TODO this might need to be a wrapper, so it can take in Scene??
            }
        }
    }

} // namespace Fenrir