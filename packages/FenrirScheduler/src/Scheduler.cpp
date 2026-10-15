#include "FenrirScheduler/Scheduler.hpp"

#include <algorithm>
#include <future>
#include <utility>
#include <vector>
namespace Fenrir
{
    namespace
    {
        thread_local SendBuffer* t_sendBuffer = nullptr;

        // Makes `buffer` this thread's send buffer while a parallel system runs, even if the system throws.
        class SendBufferScope
        {
          public:
            explicit SendBufferScope(SendBuffer& buffer)
            {
                t_sendBuffer = &buffer;
            }
            SendBufferScope(const SendBufferScope&) = delete;
            SendBufferScope& operator=(const SendBufferScope&) = delete;
            ~SendBufferScope()
            {
                t_sendBuffer = nullptr;
            }
        };
    } // namespace

    SendBuffer* SendBuffer::OfThisThread() noexcept
    {
        return t_sendBuffer;
    }

    void SendBuffer::Add(std::function<void()> send)
    {
        m_sends.push_back(std::move(send));
    }

    void SendBuffer::Flush()
    {
        for (const std::function<void()>& send : m_sends)
        {
            send();
        }
        m_sends.clear();
    }

    Scheduler::Scheduler() : Scheduler(std::thread::hardware_concurrency())
    {
    }

    // hardware_concurrency may be 0 (unknown); parallel systems need at least one worker.
    Scheduler::Scheduler(std::size_t workerThreads)
        : m_runOnceSystems(), m_systems(), m_threadPool(std::max<std::size_t>(workerThreads, 1))
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
            std::vector<SystemFunc>& systems = m_systems[priority];
            std::vector<SendBuffer> sends(systems.size());
            std::vector<std::future<void>> futures;
            for (std::size_t index = 0; index < systems.size(); ++index)
            {
                futures.emplace_back(m_threadPool.Enqueue([&app, &system = systems[index], &buffer = sends[index]] {
                    const SendBufferScope scope(buffer);
                    system(app);
                }));
            }

            for (auto& fut : futures)
            {
                fut.wait();
            }

            for (SendBuffer& buffer : sends)
            {
                buffer.Flush();
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