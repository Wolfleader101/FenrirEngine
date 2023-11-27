#pragma once

#include <memory>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirTime/Time.hpp"

#include <typeindex>
#include <unordered_map>

namespace Fenrir
{
    /**
     * @brief Interface for event queues
     * This is needed because we need to store different event queues in a single container
     */
    struct IEventQueue
    {
        virtual ~IEventQueue() = default;

      public:
        /**
         * @brief update the event queue
         *
         */
        virtual void Update() = 0;
    };

    template <typename TEvent>
    class EventQueue : public IEventQueue
    {
      public:
        void Send(const TEvent& event)
        {
            currentBuffer.push_back(event);
        }

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
    class App
    {
      public:
        App(std::unique_ptr<ILogger> logger);

        App& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        App& AddSystem(SchedulePriority priority, SystemFunc system);

        const std::unique_ptr<ILogger>& Logger() const;

        void Run();

        void Stop();

        template <typename TEvent>
        void SendEvent(const TEvent& event);

        template <typename TEvent>
        const std::vector<TEvent>& ReadEvents() const;

      private:
        Time m_time;
        Scheduler m_scheduler;
        std::unique_ptr<ILogger> m_logger;
        bool m_running = true;

        std::unordered_map<std::type_index, std::unique_ptr<IEventQueue>> m_eventQueues;

        void UpdateEvents();

        template <typename TEvent>
        EventQueue<TEvent>& GetEventQueue();
    };

    template <typename TEvent>
    void App::SendEvent(const TEvent& event)
    {
        GetEventQueue<TEvent>().Send(event);
    }

    template <typename TEvent>
    const std::vector<TEvent>& App::ReadEvents() const
    {
        return GetEventQueue<TEventArgs>().ReadEvents();
    }

    template <typename TEvent>
    EventQueue<TEvent>& App::GetEventQueue()
    {
        auto type = std::type_index(typeid(TEvent));
        auto it = m_eventQueues.find(type);
        if (it == m_eventQueues.end())
        {
            auto inserted = m_eventQueues.emplace(type, std::make_unique<EventQueue<TEvent>>());
            if (!result.second)
            {
                m_logger->LogError("Failed to create an event queue for type: " +
                                   std::string(typeid(TEventArgs).name()));
            }
            it = inserted.first;
        }
        return *static_cast<EventQueue<TEvent>*>(it->second.get());
    }
} // namespace Fenrir