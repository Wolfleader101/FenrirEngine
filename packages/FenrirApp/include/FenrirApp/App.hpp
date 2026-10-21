#pragma once

#include <memory>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirTime/Time.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fenrir
{

    /**
     * @brief The cursor of a reader that has not read yet. Its first read starts at the oldest event the queue still
     * holds and reports no misses: events sent before the reader existed are not ones it missed.
     */
    inline constexpr std::size_t NewReaderCursor = static_cast<std::size_t>(-1);

    /**
     * @brief What one type-erased read through a cursor yields (IEventQueue::ReadRawAfter): the events sent since the
     * previous read, oldest first, as up to two arrays of EventSize()-byte elements in the queue's buffers (nothing is
     * copied). Valid until the queue is next sent to or swapped. The byte form of EventRange.
     */
    struct RawEventRange
    {
        const void* older = nullptr;
        std::size_t olderCount = 0;
        const void* newer = nullptr;
        std::size_t newerCount = 0;
        /** @brief How many events were dropped before the reader got to them (it went two swaps without reading). */
        std::size_t missed = 0;

        [[nodiscard]] std::size_t Count() const noexcept
        {
            return olderCount + newerCount;
        }
    };

    namespace Detail
    {
        /**
         * @brief Where a queue's two buffers sit in the count of every event it was ever sent, and the arithmetic of
         * reading them through a cursor. Shared by EventQueue<T> and the App's queues of events known only by their
         * layout, so both rotate and read alike.
         */
        class EventCursor
        {
          public:
            /** @brief What one read through a cursor yields: how many events to skip at the start of each buffer, and
             * how many were dropped before the reader got to them. */
            struct Read
            {
                std::size_t olderSkip = 0;
                std::size_t newerSkip = 0;
                std::size_t missed = 0;
            };

            /** @brief The buffers swapped: the current one, holding `currentCount` events, became the older one. */
            void Swap(std::size_t currentCount) noexcept
            {
                const std::size_t sent = m_currentStart + currentCount;
                m_previousStart = m_currentStart;
                m_currentStart = sent;
            }

            /**
             * @brief Reads the events sent after the first `seen` (NewReaderCursor for a reader that has not read)
             * from buffers holding `previousCount` and `currentCount` events, and moves `seen` past them.
             */
            [[nodiscard]] Read ReadAfter(std::size_t& seen, std::size_t previousCount,
                                         std::size_t currentCount) const noexcept
            {
                if (seen == NewReaderCursor)
                {
                    seen = m_previousStart;
                }
                const std::size_t from = std::max(seen, m_previousStart);
                Read read;
                read.olderSkip = std::min(from - m_previousStart, previousCount);
                read.newerSkip = std::min(from > m_currentStart ? from - m_currentStart : 0, currentCount);
                read.missed = m_previousStart > seen ? m_previousStart - seen : 0;
                seen = m_currentStart + currentCount;
                return read;
            }

          private:
            // How many events were sent to the queue before the first event of each buffer.
            std::size_t m_previousStart = 0;
            std::size_t m_currentStart = 0;
        };

        /**
         * @brief Sends an event by calling `send`: now, or, from a system the scheduler runs in parallel, after its
         * batch (SendBuffer). Every send to an App's event queues goes through it.
         */
        template <typename Send>
        void SendOrHold(Send&& send)
        {
            if (SendBuffer* buffer = SendBuffer::OfThisThread())
            {
                buffer->Add(std::forward<Send>(send));
                return;
            }
            send();
        }
    } // namespace Detail

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
         * @brief Swaps the two buffers, dropping the older one's events. The App does this at the end of each frame
         * that ran a tick.
         */
        virtual void Swap() = 0;

        /**
         * @name Type-erased access
         * For code that knows an event by its name and layout rather than its C++ type (scripting). A queue of events
         * that are not trivially copyable has an EventSize() of zero and cannot be used this way.
         * @{
         */

        /** @brief Bytes in one event, or zero if the events cannot be copied byte by byte. */
        [[nodiscard]] virtual std::size_t EventSize() const noexcept = 0;

        /** @brief Alignment of one event. */
        [[nodiscard]] virtual std::size_t EventAlignment() const noexcept = 0;

        /** @brief Copies one event (EventSize() bytes) into the queue's current frame. False if the queue has no byte
         * size. From a system the scheduler runs in parallel, the copy is held and sent after the batch, as
         * App::SendEvent does. */
        virtual bool SendRaw(const void* event) = 0;

        /**
         * @brief The events sent after the first `seen` this queue was ever sent, oldest first; moves `seen` past them.
         * The cursor of an EventReader, for code that knows the event only by its layout: the caller keeps `seen` (one
         * per reader, starting at NewReaderCursor), so readers never share state. An empty range if the queue has no
         * byte size.
         */
        [[nodiscard]] virtual RawEventRange ReadRawAfter(std::size_t& seen) const = 0;
        /** @} */
    };

    /**
     * @brief The events one read of an EventReader yields: those sent since its previous read, oldest first, as up to
     * two ranges into the queue's buffers (nothing is copied). Valid until the queue is next sent to or swapped, so
     * for the rest of the system that read it unless that system sends events of the same type.
     *
     * @tparam TEvent the type of event
     */
    template <typename TEvent>
    class EventRange
    {
      public:
        /** @brief Walks the older range, then the newer one. */
        class Iterator
        {
          public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = TEvent;
            using difference_type = std::ptrdiff_t;
            using pointer = const TEvent*;
            using reference = const TEvent&;

            Iterator() = default;
            Iterator(const TEvent* at, const TEvent* end, const TEvent* next, const TEvent* nextEnd)
                : m_at(at), m_end(end), m_next(next), m_nextEnd(nextEnd)
            {
                SkipEmpty();
            }

            reference operator*() const
            {
                return *m_at;
            }
            pointer operator->() const
            {
                return m_at;
            }
            Iterator& operator++()
            {
                ++m_at;
                SkipEmpty();
                return *this;
            }
            Iterator operator++(int)
            {
                Iterator before = *this;
                ++*this;
                return before;
            }
            friend bool operator==(const Iterator& a, const Iterator& b)
            {
                return a.m_at == b.m_at;
            }

          private:
            const TEvent* m_at = nullptr;
            const TEvent* m_end = nullptr;
            const TEvent* m_next = nullptr;
            const TEvent* m_nextEnd = nullptr;

            // At the end of the first range, carry on into the second.
            void SkipEmpty()
            {
                if (m_at == m_end && m_next != m_nextEnd)
                {
                    m_at = m_next;
                    m_end = m_nextEnd;
                    m_next = m_nextEnd;
                }
            }
        };

        EventRange() = default;
        EventRange(std::span<const TEvent> older, std::span<const TEvent> newer, std::size_t missed)
            : m_older(older), m_newer(newer), m_missed(missed)
        {
        }

        /** @brief The first event, the oldest. */
        [[nodiscard]] Iterator begin() const
        {
            return {m_older.data(), m_older.data() + m_older.size(), m_newer.data(), m_newer.data() + m_newer.size()};
        }
        /** @brief Past the last event, the newest. */
        [[nodiscard]] Iterator end() const
        {
            const TEvent* last = m_newer.data() + m_newer.size();
            if (m_newer.empty())
            {
                last = m_older.data() + m_older.size();
            }
            return {last, last, last, last};
        }

        /** @brief How many events the read yielded. */
        [[nodiscard]] std::size_t size() const noexcept
        {
            return m_older.size() + m_newer.size();
        }
        /** @brief Whether the read yielded no events. */
        [[nodiscard]] bool empty() const noexcept
        {
            return size() == 0;
        }

        /**
         * @brief How many events were dropped before the reader got to them: it went two swaps without reading. Zero
         * for a reader that reads at least once per swap.
         */
        [[nodiscard]] std::size_t Missed() const noexcept
        {
            return m_missed;
        }

        /** @brief The part of the events in the older of the queue's buffers: the oldest of them. */
        [[nodiscard]] std::span<const TEvent> Older() const noexcept
        {
            return m_older;
        }

        /** @brief The part of the events in the newer of the queue's buffers, after those in Older(). */
        [[nodiscard]] std::span<const TEvent> Newer() const noexcept
        {
            return m_newer;
        }

      private:
        std::span<const TEvent> m_older;
        std::span<const TEvent> m_newer;
        std::size_t m_missed = 0;
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

        void Swap() override
        {
            // clear the older buffer, then swap: current becomes previous, and previous becomes the new current
            cursor.Swap(currentBuffer.size());
            previousBuffer.clear();
            std::swap(currentBuffer, previousBuffer);
        }

        /**
         * @brief The events sent after the first `seen` this queue was ever sent, oldest first; moves `seen` past
         * them. The cursor of an EventReader; NewReaderCursor for one that has not read yet.
         */
        EventRange<TEvent> ReadAfter(std::size_t& seen) const
        {
            const Detail::EventCursor::Read read = cursor.ReadAfter(seen, previousBuffer.size(), currentBuffer.size());
            return {std::span<const TEvent>(previousBuffer).subspan(read.olderSkip),
                    std::span<const TEvent>(currentBuffer).subspan(read.newerSkip), read.missed};
        }

        // Events that can be copied as bytes (and so sent and read by code that knows only their layout).
        static constexpr bool ByteCopyable =
            std::is_trivially_copyable_v<TEvent> && std::is_default_constructible_v<TEvent>;

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
                Detail::SendOrHold([this, copy] { Send(copy); });
                return true;
            }
            else
            {
                (void)event;
                return false;
            }
        }

        [[nodiscard]] RawEventRange ReadRawAfter(std::size_t& seen) const override
        {
            if constexpr (ByteCopyable)
            {
                const EventRange<TEvent> events = ReadAfter(seen);
                return {events.Older().data(), events.Older().size(), events.Newer().data(), events.Newer().size(),
                        events.Missed()};
            }
            else
            {
                (void)seen;
                return {};
            }
        }

      private:
        std::vector<TEvent> currentBuffer;
        std::vector<TEvent> previousBuffer;
        Detail::EventCursor cursor;
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
         * @brief Construct a new App object whose parallel systems run on `workerThreads` worker threads (at least
         * one), rather than one per hardware thread
         *
         * @param logger the logger to use
         * @param workerThreads the number of worker threads
         */
        App(std::unique_ptr<ILogger> logger, std::size_t workerThreads);

        /** @brief The worker threads parallel systems run on, shared with engine work such as physics. */
        [[nodiscard]] ThreadPool& GetThreadPool() noexcept
        {
            return m_scheduler.GetThreadPool();
        }

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
         * @brief Runs one frame as if `deltaTime` seconds had passed: adds it to the accumulator and runs the fixed
         * step as many times as that covers. Reads no clock, so a test can drive the app deterministically. The first
         * frame (from here or from Run) runs the PreInit, Init and PostInit phases first; later frames do not.
         */
        void RunFrame(double deltaTime);

        /**
         * @brief Runs the Exit phase. Run does this when it stops; code that drives the app with RunFrame calls it
         * when it is done with the app.
         */
        void RunExit();

        /**
         * @brief Stop the app
         *
         */
        void Stop();

        /**
         * @brief True until Stop is called. Code that drives the app with RunFrame loops while it is, as Run does.
         */
        [[nodiscard]] bool IsRunning() const;

        /**
         * @brief Send an event to the event queue. From a system added with AddSystems (run in parallel), the event is
         * held until the system's batch has run, then sent in the order the systems were registered.
         *
         * @tparam TEvent the type of event
         * @param event the event
         */
        template <typename TEvent>
        void SendEvent(const TEvent& event);

        /**
         * @brief Gives the queue of `TEvent` a name, so code that only knows the name can reach the same queue
         * (FindEventQueue). `SendEvent<TEvent>` and the named queue are one queue. False if the name is already given
         * to a different queue. Naming the same queue twice is fine.
         */
        template <typename TEvent>
        bool NameEvent(std::string name);

        /**
         * @brief The queue called `name`, or null. Its events are read and sent as bytes (see IEventQueue).
         */
        [[nodiscard]] IEventQueue* FindEventQueue(std::string_view name) const;

        /**
         * @brief Makes a queue of events that have no C++ type: `size` bytes each, aligned to `alignment` (at most 16),
         * with the same double buffering as every other queue. Null if the name is taken or the size or alignment is
         * unusable.
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

        /**
         * @brief Destroys the scene with this name. Other scenes stay where they are, and the active scene stays
         * active. Destroying the active scene is refused with a logged error: change the active scene first.
         */
        void DestroyScene(const std::string& name);

        Scene& GetScene(const std::string& name);

        /**
         * @brief Calls `func(Scene&)` for every scene, active or not. For code that must reach all the entities there
         * are (a component type that stops existing), not for per-frame work.
         */
        template <typename Func>
        void ForEachScene(Func&& func)
        {
            for (const std::unique_ptr<Scene>& scene : m_scenes)
            {
                func(*scene);
            }
        }

        /**
         * @brief SPIKE: the app's resource of type `T` (process-wide state a package keeps, one value per type), or
         * null. ADR-0017 and ADR-0021 assume app-wide resources; FenrirApp had none. Same shape as Scene's.
         */
        template <typename T>
        [[nodiscard]] T* GetResource()
        {
            const auto it = m_resources.find(std::type_index(typeid(T)));
            return it == m_resources.end() ? nullptr : static_cast<T*>(it->second.get());
        }

        /** @brief SPIKE: constructs the app's resource of type `T`, replacing any it had. */
        template <typename T, typename... Args>
        T& AddResource(Args&&... args)
        {
            ResourcePtr resource(new T(std::forward<Args>(args)...), &DestroyResource<T>);
            T& added = *static_cast<T*>(resource.get());
            m_resources.insert_or_assign(std::type_index(typeid(T)), std::move(resource));
            return added;
        }

        /** @brief SPIKE: destroys the app's resource of type `T`, if any. */
        template <typename T>
        void RemoveResource()
        {
            m_resources.erase(std::type_index(typeid(T)));
        }

      private:
        using ResourcePtr = std::unique_ptr<void, void (*)(void*)>;

        template <typename T>
        static void DestroyResource(void* resource)
        {
            delete static_cast<T*>(resource);
        }

        std::unordered_map<std::type_index, ResourcePtr> m_resources;

        Time m_time;
        Scheduler m_scheduler;
        std::unique_ptr<ILogger> m_logger;
        bool m_running = true;
        bool m_initialized = false;
        // The queues swap only once a tick has run since they last did, so tick readers never miss an event.
        bool m_tickedSinceSwap = false;

        // One heap allocation per scene, so creating or destroying a scene never moves another: Entity handles and
        // references to a scene stay valid.
        std::vector<std::unique_ptr<Scene>> m_scenes;
        size_t activeSceneIndex = 0;

        // Guards the maps of queues below (not the queues): parallel systems look queues up and create them.
        mutable std::mutex m_eventQueuesMutex;

        // this is mutable because GetEventQueue() is const
        mutable std::unordered_map<std::type_index, std::unique_ptr<IEventQueue>> m_eventQueues;

        // Queues known by name: typed queues given a name (owned above) and byte queues made by CreateEventQueue (owned
        // below).
        std::unordered_map<std::string, IEventQueue*> m_namedEventQueues;
        std::unordered_map<std::string, std::unique_ptr<IEventQueue>> m_byteEventQueues;

        bool BindEventName(std::string name, IEventQueue* queue);

        /** @brief Runs the init phases, the first time only. */
        void InitOnce();

        /** @brief Swaps every event queue's buffers if a tick ran since the last swap. */
        void UpdateEvents();

        /**
         * @brief Get the Event Queue object
         *
         * @tparam TEvent the type of event
         * @return EventQueue<TEvent>& the event queue
         */
        template <typename TEvent>
        EventQueue<TEvent>& GetEventQueue() const;

        template <typename TEvent>
        friend class EventReader;
    };

    /**
     * @brief What a system reads events of one type through: it remembers which events it has already seen, so each
     * reaches it once, including events sent after it last ran (ADR-0012). A system owns its readers, as a member or
     * captured in its lambda; readers are never shared between systems.
     *
     * @tparam TEvent the type of event
     */
    template <typename TEvent>
    class EventReader
    {
      public:
        /**
         * @brief The events of this type sent since this reader last read, oldest first. Each Read moves past what it
         * yields, so the next Read yields only newer events.
         */
        EventRange<TEvent> Read(const App& app)
        {
            return app.GetEventQueue<TEvent>().ReadAfter(m_seen);
        }

      private:
        std::size_t m_seen = NewReaderCursor;
    };

    template <typename TEvent>
    void App::SendEvent(const TEvent& event)
    {
        EventQueue<TEvent>& queue = GetEventQueue<TEvent>();
        Detail::SendOrHold([&queue, event] { queue.Send(event); });
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
        const std::lock_guard lock(m_eventQueuesMutex);
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