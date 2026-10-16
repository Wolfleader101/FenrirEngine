#pragma once

// Which components make physics: one definition, shared by FenrirPhysics and FenrirPhysicsScripting. Private to the
// FenrirPhysics package.

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    /**
     * @brief Whether `entity` has a body or stands for one: a RigidBody, or a Collider (a static body of its own, or
     * part of an ancestor's compound body). The entity must be alive.
     */
    [[nodiscard]] inline bool HasBodyComponent(const Entity& entity)
    {
        return entity.HasComponent<RigidBody>() || entity.HasComponent<Collider>();
    }

    /** @brief Whether any entity in `entities` has a body component (see HasBodyComponent): the scene has physics. */
    [[nodiscard]] inline bool HasBodyComponents(EntityList& entities)
    {
        return entities.Count(ComponentType::Native<RigidBody>()) > 0 ||
               entities.Count(ComponentType::Native<Collider>()) > 0;
    }
} // namespace Fenrir
