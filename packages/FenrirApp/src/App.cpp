#include "FenrirApp/App.hpp"

namespace Fenrir
{
    App::App(std::unique_ptr<ILogger> logger)
        : m_time(), m_scheduler(), m_logger(std::move(logger)), m_scenes(), m_activeScene(nullptr), m_eventQueues()
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

    void App::Stop()
    {
        m_running = false;
    }

    void App::UpdateEvents()
    {
        for (auto& [type, queue] : m_eventQueues)
        {
            queue->Update();
        }
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

            UpdateEvents();
        }
        m_scheduler.RunSystems(*this, SchedulePriority::Exit);
    }

    const Time& App::GetTime() const
    {
        return m_time;
    }

    std::shared_ptr<Scene> App::GetActiveScene()
    {
        return m_activeScene;
    }

    void App::ChangeActiveScene(const std::string& name)
    {
        auto scene = m_scenes.at(name);

        if (scene)
        {
            m_activeScene = scene;
            return;
        }

        //! if we get here, the scene doesnt exist
        m_logger->Error("Scene with name {0} does not exist", name);
    }

    std::shared_ptr<Scene> App::CreateScene(const std::string& name)
    {
        m_scenes.insert({name, std::make_shared<Scene>(name)});
        return m_scenes.at(name);
    }

    void App::DestroyScene(const std::string& name)
    {
        m_scenes.erase(name);

        //! if we get here, the scene doesnt exist
        m_logger->Error("Scene with name {0} does not exist", name);
    }

    std::shared_ptr<Scene> App::GetScene(const std::string& name)
    {
        return m_scenes.at(name);

        //! if we get here, the scene doesnt exist
        m_logger->Error("Scene with name {0} does not exist", name);
        return nullptr;
    }

} // namespace Fenrir