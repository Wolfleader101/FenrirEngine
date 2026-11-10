#pragma once

// The CollisionFilter and the collision group physics reads from an entity. Private to the FenrirPhysics package.

#include <cstdint>

#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir::Detail
{
    /** @brief The entity's CollisionFilter, or the default for one with none. */
    [[nodiscard]] inline CollisionFilter FilterOf(const Entity& entity)
    {
        return entity.HasComponent<CollisionFilter>() ? entity.GetComponent<CollisionFilter>() : CollisionFilter{};
    }

    /** @brief The group of the entity's CollisionGroup, or 0, no group, for one with none. */
    [[nodiscard]] inline std::uint32_t GroupOf(const Entity& entity)
    {
        return entity.HasComponent<CollisionGroup>() ? entity.GetComponent<CollisionGroup>().group : 0u;
    }
} // namespace Fenrir::Detail
