#pragma once

// Walks up the entity hierarchy as physics reads it. Private to the FenrirPhysics package.

#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir::Detail
{
    /** @brief The entity's parent, or an empty handle for one with none. */
    [[nodiscard]] inline Entity ParentOf(const Entity& entity)
    {
        return entity.HasComponent<Relationship>() ? entity.GetComponent<Relationship>().parent : Entity();
    }

    /** @brief How many ancestors the entity has: physics writes Transforms parents before children. */
    [[nodiscard]] inline int DepthOf(const Entity& entity)
    {
        int depth = 0;
        for (Entity at = ParentOf(entity); at.IsValid(); at = ParentOf(at))
        {
            ++depth;
        }
        return depth;
    }

    /**
     * @brief Whether `entity` hosts a compound body: it has a RigidBody and is not a trigger. A RigidBody on a
     * trigger's entity does not make the trigger move as one.
     */
    [[nodiscard]] inline bool IsBodyHost(const Entity& entity)
    {
        return entity.HasComponent<RigidBody>() && !entity.HasComponent<Trigger>();
    }
} // namespace Fenrir::Detail
