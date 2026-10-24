#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "FenrirECS/ComponentWatch.hpp"

namespace Fenrir
{
    class ComponentType;
    class EntityList;

    /**
     * @brief A tracked component set: the entities in one EntityList that have one component type, native or dynamic,
     * as of its last drain, in entity order. It is kept from a ComponentWatch and a scan of what was there when it was
     * made, so it starts full.
     *
     * Each Drain names the entities whose component of the type came or went since the one before (or since the set was
     * made), each once, even if it came back: removing a component and adding it again, or replacing one
     * (Entity::AddComponent on an entity that has it), counts. The set itself changes only at a drain. It holds no data
     * of its own about its entities: whoever drains it keeps their own records, keyed by entity.
     *
     * Several sets of one type are independent. Move-only. It may outlive its list, and then nothing more comes or
     * goes.
     */
    class TrackedComponents
    {
      public:
        /** @brief A set that tracks nothing: it is empty and drains empty. */
        TrackedComponents() noexcept = default;

        /**
         * @brief Tracks the entities in `entities` with a component of `type`, starting with those that have one now.
         * A type EntityList::Watch cannot watch gives a set that tracks nothing.
         */
        TrackedComponents(EntityList& entities, const ComponentType& type);

        TrackedComponents(TrackedComponents&& other) noexcept = default;
        TrackedComponents& operator=(TrackedComponents&& other) noexcept = default;
        TrackedComponents(const TrackedComponents&) = delete;
        TrackedComponents& operator=(const TrackedComponents&) = delete;

        ~TrackedComponents() = default;

        /**
         * @brief The entities whose component of the type came or went since the last drain, each once, in entity
         * order. Then brings the set up to date: an entity is in it if it is alive and has a component of the type now.
         */
        [[nodiscard]] std::vector<std::uint32_t> Drain();

        /** @brief The entities with a component of the type as of the last drain (or the scan), in entity order. */
        [[nodiscard]] const std::set<std::uint32_t>& Entities() const noexcept
        {
            return m_entities;
        }

        /** @brief Whether the entity had a component of the type as of the last drain (or the scan). */
        [[nodiscard]] bool Contains(std::uint32_t entity) const
        {
            return m_entities.contains(entity);
        }

      private:
        ComponentWatch m_watch;
        std::set<std::uint32_t> m_entities;
    };
} // namespace Fenrir
