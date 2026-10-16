#pragma once

// The contact and trigger events a physics world sends, once per step, in PostTick. Read them through an
// EventReader<T>. Plain data: the entities named may already be destroyed by the time an end or exit is read (their
// version tells), so every begin and enter has its end and exit.

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief Two solid colliders of different bodies began touching (a contact), and at least one of the bodies has an
     * enabled ContactEvents.
     *
     * A and B are ordered by entity id, `bodyA` first. A body falling asleep does not end a contact.
     */
    struct ContactBegin
    {
        /** @brief The rigid body entities. */
        Entity bodyA;
        Entity bodyB;
        /** @brief The collider entities touching (each its body's entity, or one beneath it in a compound body). */
        Entity colliderA;
        Entity colliderB;
        /** @brief Where they touch, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief World space, unit length, pointing from A to B. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How fast A and B were closing along `normal` when they began touching, in metres per second. */
        float relativeSpeed = 0.0f;
    };

    /**
     * @brief A contact reported by ContactBegin ended: the colliders separated, one of the bodies or colliders was
     * removed, or ContactEvents was removed or disabled on both bodies. Names the same entities, in the same order, as
     * its ContactBegin.
     */
    struct ContactEnd
    {
        Entity bodyA;
        Entity bodyB;
        Entity colliderA;
        Entity colliderB;
    };

    /** @brief A collider began overlapping an enabled trigger. */
    struct TriggerEnter
    {
        /** @brief The trigger's collider entity. */
        Entity trigger;
        /** @brief The rigid body entity of the collider that entered. */
        Entity otherBody;
        /** @brief The collider entity that entered. */
        Entity otherCollider;
    };

    /**
     * @brief A collider that entered a trigger stopped overlapping it: it left, one of them was removed, or the trigger
     * was disabled. Names the same entities as its TriggerEnter. A body falling asleep inside a trigger does not exit.
     */
    struct TriggerExit
    {
        Entity trigger;
        Entity otherBody;
        Entity otherCollider;
    };
} // namespace Fenrir
