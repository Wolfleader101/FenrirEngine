#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Fenrir
{
    class EntityList;

    /** @brief One thing a ComponentWatch saw happen: an entity gained or lost a component of the watched type. */
    struct ComponentChange
    {
        enum class Kind : std::uint8_t
        {
            Added,
            Removed,
        };

        std::uint32_t entity;
        Kind kind;

        friend bool operator==(const ComponentChange&, const ComponentChange&) = default;
    };

    namespace Detail
    {
        // Where a watch's changes go. It stays at one address for the watch's life, so the watched storage can point
        // at it. Private to the ECS: only ComponentWatch and EntityList use it.
        struct WatchRecord
        {
            std::vector<ComponentChange> changes;

            // The watched storage (opaque, so that no ECS library type appears here) and what starts or stops it
            // recording here. Null when the watch watches nothing.
            void* storage = nullptr;
            void (*hook)(void* storage, WatchRecord& record, bool start) = nullptr;

            // Expires with the EntityList the storage belongs to: after that there is nothing to stop.
            std::weak_ptr<const void> list;
        };
    } // namespace Detail

    /**
     * @brief Records, in the order they happen, every add and remove of one component type in one EntityList, until
     * drained. Made by EntityList::Watch.
     *
     * Each change is a fact: adding a component and removing it again before a drain appears as both, and replacing one
     * (Entity::AddComponent on an entity that has it) appears as a removal then an add. Destroying an entity, clearing
     * the list, or clearing the type records a removal for each component that goes. Edits to a component's value are
     * not changes. A watch sees only what happens after it is made; several watches on one type record independently.
     *
     * Move-only. Destroying it stops the recording, after which it costs the list nothing. It may outlive its list, and
     * then records nothing more.
     */
    class ComponentWatch
    {
      public:
        /** @brief A watch that watches nothing: it records nothing and drains empty. */
        ComponentWatch() noexcept = default;

        ComponentWatch(ComponentWatch&& other) noexcept = default;
        ComponentWatch& operator=(ComponentWatch&& other) noexcept;
        ComponentWatch(const ComponentWatch&) = delete;
        ComponentWatch& operator=(const ComponentWatch&) = delete;

        ~ComponentWatch();

        /** @brief Every change recorded since the watch was made or last drained, oldest first; the record is then
         * empty. */
        [[nodiscard]] std::vector<ComponentChange> Drain();

      private:
        friend class EntityList;

        explicit ComponentWatch(std::unique_ptr<Detail::WatchRecord> record) noexcept : m_record(std::move(record))
        {
        }

        void Stop() noexcept;

        std::unique_ptr<Detail::WatchRecord> m_record;
    };
} // namespace Fenrir
