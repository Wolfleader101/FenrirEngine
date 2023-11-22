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
        m_scheduler.Init();

        while (m_running)
        {
            // TODO eventually make this a loop
            m_scheduler.Run();
        }
    }

    Scheduler::Scheduler()
    {
    }

    bool Scheduler::IsRunOnceSystem(SchedulePriority priority)
    {
        return priority == SchedulePriority::PreInit || priority == SchedulePriority::Init ||
               priority == SchedulePriority::PostInit;
    }

    Scheduler& Scheduler::AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems)
    {
        // for each system, add it to the map
        for (auto system : systems)
        {
            AddSystem(priority, system);
        }

        return *this;
    }

    Scheduler& Scheduler::AddSystem(SchedulePriority priority, SystemFunc system)
    {
        if (IsRunOnceSystem(priority))
        {
            m_runOnceSystems[priority].push_back(system);
        }
        else
        {
            m_updateSystems[priority].push_back(system);
        }
        return *this;
    }

    void Scheduler::Init()
    {
        // for each priority, run each system in order of their insertion
        for (auto& [priority, systems] : m_runOnceSystems)
        {
            for (auto& system : systems)
            {
                system(); // TODO this might need to be a wrapper, so it can take in Scene??
            }
        }
    }

    void Scheduler::Run()
    {
        // for each priority, run each system in order of their insertion
        for (auto& [priority, systems] : m_updateSystems)
        {
            for (auto& system : systems)
            {
                system(); // TODO this might need to be a wrapper, so it can take in Scene??
            }
        }
    }

} // namespace Fenrir