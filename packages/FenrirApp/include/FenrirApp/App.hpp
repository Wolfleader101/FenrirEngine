#pragma once

#include <memory>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirTime/Time.hpp"

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Fenrir
{

    /**
     * @brief The events a queue holds for reading, as one contiguous array of `count` elements of EventSize() bytes each.
     * Valid until the queue is next sent to or updated.
     */
    struct RawEvents
    {
        const void* data = nullptr;
        std::size_t count = 0;
    };

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

        /**
         * @name Type-erased access
         * For code that knows an event by its name and layout rather than its C++ type (scripting). A queue of events that
         * are not trivially copyable has an EventSize() of zero and cannot be used this way.
         * @{
         */

        /** @brief Bytes in one event, or zero if the events cannot be copied byte by byte. */
        [[nodiscard]] virtual std::size_t EventSize() const noexcept = 0;

        /** @brief Alignment of one event. */
        [[nodiscard]] virtual std::size_t EventAlignment() const noexcept = 0;

        /** @brief Copies one event (EventSize() bytes) into the queue's current frame. False if the queue has no byte size. */
        virtual bool SendRaw(const void* event) = 0;

        /** @brief The current and the previous frame's events, the same window ReadEvents gives. */
        [[nodiscard]] virtual RawEvents ReadRaw() const = 0;
        /** @} */
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

        // Events that can be copied as bytes (and so sent and read by code that knows only their layout).
        static constexpr bool ByteCopyable = std::is_trivially_copyable_v<TEvent> && std::is_default_constructible_v<TEvent>;

        [[nodiscard]] std::size_t EventSize() const noexcept override
        {
            return ByteCopyable ? sizeof(TEvent) : 0;
        }

        [[nodiscard]] std::size_t EventAlignment() const noexcept override
        {
            return alignof(TEvent);
        }

        bool SendRaw(const void* event) override
        {
            if constexpr (ByteCopyable)
            {
                TEvent copy;
                std::memcpy(&copy, event, sizeof(TEvent));
                currentBuffer.push_back(copy);
                return true;
            }
            else
            {
                (void)event;
                return false;
            }
        }

        [[nodiscard]] RawEvents ReadRaw() const override
        {
            const auto& events = ReadEvents();
            return {events.data(), events.size()};
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
         * @brief Add systems to the scheduler that run in parallel
         *
         * @param priority the priority of the systems
         * @param systems the system functions
         * @return App& the app
         */
        App& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        /**
         * @brief Add systems to the scheduler that run sequentially
         *
         * @param priority the priority of the systems
         * @param systems the system functions
         * @return App& the app
         */
        App& AddSequentialSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

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
         * @brief Gives the queue of `TEvent` a name, so code that only knows the name can reach the same queue (FindEventQueue).
         * `SendEvent<TEvent>` and the named queue are one queue. False if the name is already given to a different queue.
         * Naming the same queue twice is fine.
         */
        template <typename TEvent>
        bool NameEvent(std::string name);

        /**
         * @brief The queue called `name`, or null. Its events are read and sent as bytes (see IEventQueue).
         */
        [[nodiscard]] IEventQueue* FindEventQueue(std::string_view name) const;

        /**
         * @brief Makes a queue of events that have no C++ type: `size` bytes each, aligned to `alignment` (at most 16), with the
         * same double buffering as every other queue. Null if the name is taken or the size or alignment is unusable.
         */
        IEventQueue* CreateEventQueue(std::string name, std::size_t size, std::size_t alignment);

        /** @brief Removes a queue made by CreateEventQueue, and its name. False if there is none by that name. */
        bool RemoveEventQueue(std::string_view name);

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

        /**
         * @brief Calls `func(Scene&)` for every scene, active or not. For code that must reach all the entities there are (a
         * component type that stops existing), not for per-frame work.
         */
        template <typename Func>
        void ForEachScene(Func&& func)
        {
            for (Scene& scene : m_scenes)
            {
                func(scene);
            }
        }

      private:
        Time m_time;
        Scheduler m_scheduler;
        std::unique_ptr<ILogger> m_logger;
        bool m_running = true;

        std::vector<Scene> m_scenes;
        size_t activeSceneIndex = 0;

        // this is mutable because GetEventQueue() is const
        mutable std::unordered_map<std::type_index, std::unique_ptr<IEventQueue>> m_eventQueues;

        // Queues known by name: typed queues given a name (owned above) and byte queues made by CreateEventQueue (owned below).
        std::unordered_map<std::string, IEventQueue*> m_namedEventQueues;
        std::unordered_map<std::string, std::unique_ptr<IEventQueue>> m_byteEventQueues;

        bool BindEventName(std::string name, IEventQueue* queue);

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
    bool App::NameEvent(std::string name)
    {
        return BindEventName(std::move(name), &GetEventQueue<TEvent>());
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