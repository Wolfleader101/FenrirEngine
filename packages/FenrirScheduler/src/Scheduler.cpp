#include "FenrirScheduler/Scheduler.hpp"

namespace Fenrir
{

    Scheduler::Scheduler()
    {
    }

    bool Scheduler::IsRunOnceSystem(SchedulePriority priority)
    {
        return priority == SchedulePriority::PreInit || priority == SchedulePriority::Init ||
               priority == SchedulePriority::PostInit;
    }

    void Scheduler::RunSystems(App& app, SchedulePriority priority)
    {
        if (m_systems.find(priority) != m_systems.end())
        {
            for (auto& system : m_systems[priority])
            {
                system(app);
            }
        }
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
            m_systems[priority].push_back(system);
        }
        return *this;
    }

    void Scheduler::Init(App& app)
    {
        // for each priority, run each system in order of their insertion
        for (auto& [priority, systems] : m_runOnceSystems)
        {
            for (auto& system : systems)
            {
                system(app); // TODO this might need to be a wrapper, so it can take in Scene??
            }
        }
    }
} // namespace Fenrir