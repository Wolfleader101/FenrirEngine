#pragma once

// PROTOTYPE (physics spike): world transforms computed on demand from Transform and Relationship.

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /** @brief Position, rotation and (lossy under rotation) scale in the scene, composed up the parent chain. */
    Transform GetWorldTransform(const Entity& entity);

    /** @brief Sets the entity's local Transform so that its world transform is `world`. */
    void SetWorldTransform(const Entity& entity, const Transform& world);
} // namespace Fenrir
