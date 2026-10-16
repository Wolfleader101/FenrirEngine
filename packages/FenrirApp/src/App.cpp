#include "FenrirApp/App.hpp"

#include <thread>

namespace Fenrir
{
    // As Scheduler() does: one worker thread per hardware thread.
    App::App(std::unique_ptr<ILogger> logger) : App(std::move(logger), std::thread::hardware_concurrency())
    {
    }

    App::App(std::unique_ptr<ILogger> logger, std::size_t workerThreads)
        : m_time(), m_scheduler(workerThreads), m_logger(std::move(logger)), m_scenes(), m_eventQueues()
    {
        m_scenes.push_back(std::make_unique<Scene>("Default"));
    }

    App& App::AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems)
    {
        m_scheduler.AddSystems(priority, systems);
        return *this;
    }
    App& App::AddSequentialSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems)
    {
        m_scheduler.AddSequentialSystems(priority, systems);
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
        for (auto& [name, queue] : m_byteEventQueues)
        {
            queue->Update();
        }
    }

    namespace
    {
        // The queue for an event that has no C++ type: the same two buffers and the same rotation as EventQueue<T>,
        // over bytes.
        class ByteEventQueue final : public IEventQueue
        {
          public:
            ByteEventQueue(std::size_t size, std::size_t alignment) : m_size(size), m_alignment(alignment)
            {
            }

            void Update() override
            {
                m_previous.clear();
                std::swap(m_current, m_previous);
                m_dirty = true;
            }

            [[nodiscard]] std::size_t EventSize() const noexcept override
            {
                return m_size;
            }

            [[nodiscard]] std::size_t EventAlignment() const noexcept override
            {
                return m_alignment;
            }

            bool SendRaw(const void* event) override
            {
                const auto* bytes = static_cast<const std::byte*>(event);
                m_current.insert(m_current.end(), bytes, bytes + m_size);
                m_dirty = true;
                return true;
            }

            [[nodiscard]] RawEvents ReadRaw() const override
            {
                // Rebuilt only when something changed, so a view handed out earlier stays good while nothing is sent.
                if (m_dirty)
                {
                    m_combined.clear();
                    m_combined.insert(m_combined.end(), m_previous.begin(), m_previous.end());
                    m_combined.insert(m_combined.end(), m_current.begin(), m_current.end());
                    m_dirty = false;
                }
                return {m_combined.data(), m_combined.size() / m_size};
            }

          private:
            std::size_t m_size;
            std::size_t m_alignment;
            std::vector<std::byte> m_current;
            std::vector<std::byte> m_previous;
            mutable std::vector<std::byte> m_combined;
            mutable bool m_dirty = true;
        };
    } // namespace

    bool App::BindEventName(std::string name, IEventQueue* queue)
    {
        if (name.empty() || m_byteEventQueues.contains(name))
        {
            return false;
        }
        const auto [at, added] = m_namedEventQueues.emplace(std::move(name), queue);
        return added || at->second == queue;
    }

    IEventQueue* App::FindEventQueue(std::string_view name) const
    {
        const auto found = m_namedEventQueues.find(std::string(name));
        return found == m_namedEventQueues.end() ? nullptr : found->second;
    }

    IEventQueue* App::CreateEventQueue(std::string name, std::size_t size, std::size_t alignment)
    {
        // The storage is a std::vector of bytes, which is aligned for anything up to the allocator's guarantee.
        constexpr std::size_t maxAlignment = 16;
        const bool usable = !name.empty() && size > 0 && alignment > 0 && alignment <= maxAlignment &&
                            (alignment & (alignment - 1)) == 0 && size % alignment == 0;
        if (!usable || m_namedEventQueues.contains(name))
        {
            return nullptr;
        }
        auto queue = std::make_unique<ByteEventQueue>(size, alignment);
        IEventQueue* raw = queue.get();
        m_namedEventQueues.emplace(name, raw);
        m_byteEventQueues.emplace(std::move(name), std::move(queue));
        return raw;
    }

    bool App::RemoveEventQueue(std::string_view name)
    {
        const std::string key(name);
        if (m_byteEventQueues.erase(key) == 0)
        {
            return false;
        }
        m_namedEventQueues.erase(key);
        return true;
    }

    void App::InitOnce()
    {
        if (!m_initialized)
        {
            m_initialized = true;
            m_scheduler.Init(*this);
        }
    }

    void App::Run()
    {
        // Before the first measurement, so the first frame's delta covers the init phases, as it always has.
        InitOnce();

        while (m_running)
        {
            RunFrame(m_time.MeasureFrame());
        }
        RunExit();
    }

    void App::RunExit()
    {
        m_scheduler.RunSystems(*this, SchedulePriority::Exit);
    }

    void App::RunFrame(double deltaTime)
    {
        InitOnce();

        m_time.Advance(deltaTime);

        m_scheduler.RunSystems(*this, SchedulePriority::PreUpdate);

        m_scheduler.RunSystems(*this, SchedulePriority::BeforeTicks);

        int ticks = 0;
        while (m_time.accumulator >= m_time.tickRate && ticks < m_time.maxTicksPerFrame)
        {
            m_scheduler.RunSystems(*this, SchedulePriority::PreTick);
            m_scheduler.RunSystems(*this, SchedulePriority::Tick);
            m_scheduler.RunSystems(*this, SchedulePriority::PostTick);

            m_time.accumulator -= m_time.tickRate;
            ++ticks;
        }

        // Still owing a whole tick after the cap: drop everything owed, so a slow frame cannot make the next one
        // slower still.
        if (m_time.accumulator >= m_time.tickRate)
        {
            m_logger->Warn("A frame owed more than {} ticks; dropped {:.4f} s of simulation time",
                           m_time.maxTicksPerFrame, m_time.accumulator);
            m_time.accumulator = 0.0;
        }

        m_scheduler.RunSystems(*this, SchedulePriority::AfterTicks);

        m_scheduler.RunSystems(*this, SchedulePriority::Update);

        m_scheduler.RunSystems(*this, SchedulePriority::PostUpdate);

        UpdateEvents();
    }

    const Time& App::GetTime() const
    {
        return m_time;
    }

    Scene& App::GetActiveScene()
    {
        return *m_scenes.at(activeSceneIndex);
    }

    void App::ChangeActiveScene(const std::string& name)
    {

        for (size_t i = 0; i < m_scenes.size(); i++)
        {
            if (m_scenes.at(i)->GetName() == name)
            {
                activeSceneIndex = i;
                return;
            }
        }

        //! if we get here, the scene doesnt exist
        m_logger->Error("Scene with name {0} does not exist", name);
    }

    Scene& App::CreateScene(const std::string& name)
    {
        m_scenes.push_back(std::make_unique<Scene>(name));
        return *m_scenes.back();
    }

    void App::DestroyScene(const std::string& name)
    {
        for (size_t i = 0; i < m_scenes.size(); i++)
        {
            if (m_scenes.at(i)->GetName() == name)
            {
                if (i == activeSceneIndex)
                {
                    m_logger->Error("Scene {0} is active and cannot be destroyed; change the active scene first", name);
                    return;
                }
                m_scenes.erase(m_scenes.begin() + static_cast<std::ptrdiff_t>(i));
                if (i < activeSceneIndex)
                {
                    --activeSceneIndex;
                }
                return;
            }
        }

        //! if we get here, the scene doesnt exist
        m_logger->Error("Scene with name {0} does not exist", name);
    }

    Scene& App::GetScene(const std::string& name)
    {

        for (size_t i = 0; i < m_scenes.size(); i++)
        {
            if (m_scenes.at(i)->GetName() == name)
            {
                return *m_scenes.at(i);
            }
        }

        //! if we get here, the scene doesnt exist
        m_logger->Error("Scene with name {0} does not exist", name);
        return *m_scenes.at(0);
    }

} // namespace Fenrir