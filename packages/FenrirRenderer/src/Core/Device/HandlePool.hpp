#pragma once

// SPIKE: generational slots behind the device's handles, shared by the backends. Freed slots are reused with a bumped
// generation, so a stale handle finds nothing.

#include "Device/Device.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace Fenrir::Gpu
{
    template <typename H, typename T>
    class HandlePool
    {
      public:
        H Add(T value)
        {
            if (!m_free.empty())
            {
                const std::uint32_t index = m_free.back();
                m_free.pop_back();
                Slot& slot = m_slots[index];
                slot.value = std::move(value);
                slot.live = true;
                return {index, slot.generation};
            }
            m_slots.push_back({std::move(value), 1, true});
            return {static_cast<std::uint32_t>(m_slots.size() - 1), 1};
        }

        T* Get(H handle)
        {
            if (!handle.IsValid() || handle.index >= m_slots.size())
            {
                return nullptr;
            }
            Slot& slot = m_slots[handle.index];
            return slot.live && slot.generation == handle.generation ? &slot.value : nullptr;
        }

        /** @brief Ends the slot now; the caller has already waited out the frames in flight. */
        bool Remove(H handle, T* out = nullptr)
        {
            T* value = Get(handle);
            if (value == nullptr)
            {
                return false;
            }
            if (out != nullptr)
            {
                *out = std::move(*value);
            }
            Slot& slot = m_slots[handle.index];
            slot.live = false;
            slot.value = T{};
            ++slot.generation;
            m_free.push_back(handle.index);
            return true;
        }

        [[nodiscard]] std::uint32_t LiveCount() const
        {
            return static_cast<std::uint32_t>(m_slots.size() - m_free.size());
        }

      private:
        struct Slot
        {
            T value;
            std::uint32_t generation = 1;
            bool live = false;
        };
        std::vector<Slot> m_slots;
        std::vector<std::uint32_t> m_free;
    };

    /** @brief Destruction deferred by frame: a handle queued in frame N is freed once frame N + framesInFlight begins.
     */
    template <typename H>
    class DeferredFrees
    {
      public:
        void Queue(H handle, std::uint64_t frame)
        {
            m_queue.push_back({handle, frame});
        }

        template <typename F>
        void Collect(std::uint64_t frame, std::uint64_t framesInFlight, F&& free)
        {
            while (!m_queue.empty() && m_queue.front().frame + framesInFlight <= frame)
            {
                free(m_queue.front().handle);
                m_queue.pop_front();
            }
        }

      private:
        struct Entry
        {
            H handle;
            std::uint64_t frame;
        };
        std::deque<Entry> m_queue;
    };
} // namespace Fenrir::Gpu
