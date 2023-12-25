#include "FenrirScheduler/Scheduler.hpp"

#include <future>
#include <vector>
namespace Fenrir
{

    Scheduler::Scheduler() : m_runOnceSystems(), m_systems(), m_threadPool(std::thread::hardware_concurrency())
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
            std::vector<std::future<void>> futures;
            for (auto& system : m_systems[priority])
            {
                // system(app);
                futures.emplace_back(m_threadPool.enqueue([&app, &system] { system(app); }));
            }

            for (auto& fut : futures)
            {
                fut.wait();
            }
        }

        RunSequentialSystems(app, priority);
    }

    void Scheduler::RunSequentialSystems(App& app, SchedulePriority priority)
    {
        if (m_sequentialSystems.find(priority) != m_sequentialSystems.end())
        {
            for (auto& system : m_sequentialSystems[priority])
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

    Scheduler& Scheduler::AddSequentialSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems)
    {
        for (auto system : systems)
        {
            AddSequentialSystem(priority, system);
        }

        return *this;
    }

    Scheduler& Scheduler::AddSequentialSystem(SchedulePriority priority, SystemFunc system)
    {
        m_sequentialSystems[priority].push_back(system);
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