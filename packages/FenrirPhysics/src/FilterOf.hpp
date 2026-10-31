#pragma once

// The CollisionFilter physics reads from an entity. Private to the FenrirPhysics package.

#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir::Detail
{
    /** @brief The entity's CollisionFilter, or the default for one with none. */
    [[nodiscard]] inline CollisionFilter FilterOf(const Entity& entity)
    {
        return entity.HasComponent<CollisionFilter>() ? entity.GetComponent<CollisionFilter>() : CollisionFilter{};
    }

    /** @brief The entity's CollisionGroup's group, or 0 (no group) for one with none. */
    [[nodiscard]] inline std::uint32_t GroupOf(const Entity& entity)
    {
        return entity.HasComponent<CollisionGroup>() ? entity.GetComponent<CollisionGroup>().group : 0u;
    }
} // namespace Fenrir::Detail
