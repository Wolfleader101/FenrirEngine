#include "FenrirApp/App.hpp"

namespace Fenrir
{
    App::App(std::unique_ptr<ILogger> logger) : m_time(), m_scheduler(), m_logger(std::move(logger))
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

    const std::unique_ptr<ILogger>& App::Logger() const
    {
        return m_logger;
    }

    void App::Run()
    {
        m_scheduler.Init(*this);

        while (m_running)
        {
            m_time.Update();

            m_scheduler.RunSystems(*this, SchedulePriority::PreUpdate);

            while (m_time.accumulator >= m_time.tickRate)
            {
                m_scheduler.RunSystems(*this, SchedulePriority::Tick);

                m_time.accumulator -= m_time.tickRate;
            }

            m_scheduler.RunSystems(*this, SchedulePriority::Update);

            m_scheduler.RunSystems(*this, SchedulePriority::PostUpdate);

            // m_scheduler.RunSystems(*this, SchedulePriority::LastUpdate);
        }
    }

} // namespace Fenrir