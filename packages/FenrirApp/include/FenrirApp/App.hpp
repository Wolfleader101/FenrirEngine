#pragma once

#include <memory>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirTime/Time.hpp"

#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Fenrir
{
    /**
     * @brief Interface for event queues
     * This is needed because we need to store different event queues in a single container
     */
    struct IEventQueue
    {
        /**
         * @brief Destroy the IEventQueue object
         *
         */
        virtual ~IEventQueue() = default;

      public:
        /**
         * @brief update the event queue
         *
         */
        virtual void Update() = 0;
    };

    /**
     * @brief A queue for events of a specific type
     *
     * @tparam TEvent the type of event
     */
    template <typename TEvent>
    class EventQueue : public IEventQueue
    {
      public:
        /**
         * @brief Send an event to the queue
         *
         * @param event the event
         */
        void Send(const TEvent& event)
        {
            currentBuffer.push_back(event);
        }

        /**
         * @brief Update the event queue
         *
         */
        void Update() override
        {
            // clear the buffer that is two frames old
            previousBuffer.clear();

            // swap the buffers: current becomes previous, and previous becomes the new current
            std::swap(currentBuffer, previousBuffer);
        }

        /**
         * @brief Read the events from the queue
         *
         * @return const std::vector<TEvent>& the events
         */
        const std::vector<TEvent>& ReadEvents() const
        {
            // combine both current and previous buffers for reading
            combinedBuffer.clear();
            combinedBuffer.insert(combinedBuffer.end(), previousBuffer.begin(), previousBuffer.end());
            combinedBuffer.insert(combinedBuffer.end(), currentBuffer.begin(), currentBuffer.end());
            return combinedBuffer;
        }

      private:
        std::vector<TEvent> currentBuffer;
        std::vector<TEvent> previousBuffer;
        mutable std::vector<TEvent> combinedBuffer; // marked as mutable because ReadEvents() is const
    };

    /**
     * @brief The main app class
     *
     * This class is responsible for running the update loop and managing the scheduler
     * It is also responsible for setup of all of the modular engine systems passed in, and handling all events
     *
     */
    class App
    {
      public:
        /**
         * @brief Construct a new App object
         *
         * @param logger the logger to use
         */
        App(std::unique_ptr<ILogger> logger);

        /**
         * @brief Add systems to the scheduler
         *
         * @param priority the priority of the systems
         * @param systems the system functions
         * @return App& the app
         */
        App& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        /**
         * @brief Add a system to the scheduler
         *
         * @param priority the priority of the system
         * @param system the system function
         * @return App& the app
         */
        App& AddSystem(SchedulePriority priority, SystemFunc system);

        /**
         * @brief Get the Logger object
         *
         * @return const std::unique_ptr<ILogger>& the logger
         */
        const std::unique_ptr<ILogger>& Logger() const;

        /**
         * @brief Run the app and start update loop
         *
         */
        void Run();

        /**
         * @brief Stop the app
         *
         */
        void Stop();

        /**
         * @brief Send an event to the event queue
         *
         * @tparam TEvent the type of event
         * @param event the event
         */
        template <typename TEvent>
        void SendEvent(const TEvent& event);

        /**
         * @brief Read the events from the queue
         *
         * @tparam TEvent the type of event
         * @return const std::vector<TEvent>& the events
         */
        template <typename TEvent>
        const std::vector<TEvent>& ReadEvents() const;

        /**
         * @brief Get the Time object
         *
         * @return const Time& the time
         */
        const Time& GetTime() const;

        Scene& GetActiveScene();

        void ChangeActiveScene(const std::string& name);

        Scene& CreateScene(const std::string& name);

        void DestroyScene(const std::string& name);

        Scene& GetScene(const std::string& name);

      private:
        Time m_time;
        Scheduler m_scheduler;
        std::unique_ptr<ILogger> m_logger;
        bool m_running = true;

        std::vector<Scene> m_scenes;
        size_t activeSceneIndex = 0;

        // this is mutable because GetEventQueue() is const
        mutable std::unordered_map<std::type_index, std::unique_ptr<IEventQueue>> m_eventQueues;

        /**
         * @brief Update the event queues
         *
         */
        void UpdateEvents();

        /**
         * @brief Get the Event Queue object
         *
         * @tparam TEvent the type of event
         * @return EventQueue<TEvent>& the event queue
         */
        template <typename TEvent>
        EventQueue<TEvent>& GetEventQueue() const;
    };

    template <typename TEvent>
    void App::SendEvent(const TEvent& event)
    {
        GetEventQueue<TEvent>().Send(event);
    }

    template <typename TEvent>
    const std::vector<TEvent>& App::ReadEvents() const
    {
        return GetEventQueue<TEvent>().ReadEvents();
    }

    template <typename TEvent>
    EventQueue<TEvent>& App::GetEventQueue() const
    {
        auto type = std::type_index(typeid(TEvent));
        auto it = m_eventQueues.find(type);
        if (it == m_eventQueues.end())
        {
            auto inserted = m_eventQueues.emplace(type, std::make_unique<EventQueue<TEvent>>());
            if (!inserted.second)
            {
                m_logger->Error("Failed to create an event queue for type: " + std::string(typeid(TEvent).name()));
            }
            it = inserted.first;
        }
        return *static_cast<EventQueue<TEvent>*>(it->second.get());
    }
} // namespace Fenrir