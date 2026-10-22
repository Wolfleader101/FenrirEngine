#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace Fenrir::Graphics
{
    /**
     * @brief The slots behind one kind of device handle, for every backend to keep its objects in. A destroyed object
     * is stale at once but keeps its slot until the frame `framesInFlight` after its destruction begins; then the slot
     * is freed, its generation moves on, and it can be reused, so the old handle can never name the new object.
     *
     * @tparam H The handle type.
     * @tparam T What the backend keeps per object (nothing for the null backend, a GL name for OpenGL).
     */
    template <typename H, typename T>
    class ResourceTable
    {
      public:
        /** @brief Keeps `value` in a free slot, reusing the oldest freed one, and returns its handle. */
        H Add(T value)
        {
            if (!m_free.empty())
            {
                const std::uint32_t index = m_free.front();
                m_free.pop_front();
                Slot& slot = m_slots[index];
                slot.value = std::move(value);
                slot.state = State::Live;
                return H{index, slot.generation};
            }
            m_slots.push_back(Slot{std::move(value), 1, State::Live});
            return H{static_cast<std::uint32_t>(m_slots.size() - 1), 1};
        }

        /** @brief The live object `handle` names; null if the handle is empty or stale. */
        [[nodiscard]] T* Find(H handle)
        {
            return IsLive(handle) ? &m_slots[handle.index].value : nullptr;
        }

        /** @brief Whether `handle` names a live object. */
        [[nodiscard]] bool IsLive(H handle) const
        {
            return !handle.IsEmpty() && handle.index < m_slots.size() &&
                   m_slots[handle.index].generation == handle.generation && m_slots[handle.index].state == State::Live;
        }

        /**
         * @brief Makes a live object stale now and queues its slot to be freed by Collect once no frame in flight can
         * use it. False for an empty or stale handle.
         */
        bool Destroy(H handle, std::uint64_t frameIndex)
        {
            if (!IsLive(handle))
            {
                return false;
            }
            m_slots[handle.index].state = State::Destroyed;
            m_pending.push_back(Pending{handle.index, frameIndex});
            return true;
        }

        /**
         * @brief Frees the slots destroyed `framesInFlight` or more frames before `frameIndex`, calling `release` with
         * each one's object first so the backend can delete what it holds.
         */
        template <typename F>
        void Collect(std::uint64_t frameIndex, std::uint32_t framesInFlight, F&& release)
        {
            while (!m_pending.empty() && m_pending.front().frameIndex + framesInFlight <= frameIndex)
            {
                Slot& slot = m_slots[m_pending.front().index];
                release(slot.value);
                slot.value = T{};
                slot.state = State::Free;
                // Generation 0 marks an empty handle, so a wrapped counter skips it.
                if (++slot.generation == 0)
                {
                    slot.generation = 1;
                }
                m_free.push_back(m_pending.front().index);
                m_pending.pop_front();
            }
        }

        /** @brief How many objects are live. */
        [[nodiscard]] std::size_t LiveCount() const
        {
            return m_slots.size() - m_free.size() - m_pending.size();
        }

        /** @brief How many destroyed objects still hold their slots, waiting out the frames in flight. */
        [[nodiscard]] std::size_t PendingCount() const
        {
            return m_pending.size();
        }

        /** @brief Calls `visit` with every live or pending object, as a backend's teardown releases them. */
        template <typename F>
        void ForEachHeld(F&& visit)
        {
            for (Slot& slot : m_slots)
            {
                if (slot.state != State::Free)
                {
                    visit(slot.value);
                }
            }
        }

      private:
        enum class State : std::uint8_t
        {
            Free,
            Live,
            Destroyed,
        };

        struct Slot
        {
            T value;
            std::uint32_t generation = 1;
            State state = State::Free;
        };

        struct Pending
        {
            std::uint32_t index = 0;
            std::uint64_t frameIndex = 0;
        };

        std::vector<Slot> m_slots;
        std::deque<std::uint32_t> m_free;
        std::deque<Pending> m_pending;
    };
} // namespace Fenrir::Graphics
