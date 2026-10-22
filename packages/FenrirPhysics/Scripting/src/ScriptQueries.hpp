#pragma once

// The query types as C# holds them: FenrirPhysics's QueryFilter, hits and Contact (FenrirPhysics/Queries.hpp) with each
// entity as its 32-bit value, a C# Entity, since a Fenrir::Entity also holds a pointer to its list. The natives convert
// between these and FenrirPhysics's own. This header is the binding's own API description
// (Scripting/api/FenrirPhysicsScripting.api.toml), so their C# mirrors, mappings and defaults test are generated from
// it, and they are checked against C# as scripts load. The doc comments are C#'s.

#include <cstdint>

#include "FenrirMath/Math.hpp"

namespace Fenrir::ScriptQueries
{
    /** @brief A C# Entity: an entity's 32-bit value alone. */
    struct EntityValue
    {
        EntityValue() = default;

        // Named after the component it sets (the leading underscore is ignored), as an API description needs.
        constexpr explicit EntityValue(std::uint32_t _value) : value(_value)
        {
        }

        std::uint32_t value = 0;
    };

    /** @brief Which bodies a query can find. New, it finds every body except triggers. */
    struct QueryFilter
    {
        /** @brief The collision layers to find, one bit per layer: a body is found if it is in any of them. */
        std::uint32_t mask = ~0u;
        /**
         * @brief The layers the query is in, one bit per layer: when non-zero, a body is found only if its
         * CollisionFilter mask includes one of them, the rule two bodies collide by. 0 when new, which ignores bodies'
         * masks, so a ray still finds a hitbox that collides with nothing.
         *
         * To find what a body would collide with, filter as it does: `new QueryFilter { Mask = cf.Mask, Layers =
         * cf.Layers, IgnoreBody = me }`, with `cf` its CollisionFilter (or a new one).
         */
        std::uint32_t layers = 0;
        /** @brief A body never found, such as the one casting the query. None when new. */
        EntityValue ignoreBody = EntityValue(0xFFFFFFFFu);
        /** @brief Whether triggers are found. */
        bool hitTriggers = false;
    };

    /** @brief Where a ray hit a body. */
    struct RaycastHit
    {
        /** @brief The entity of the body hit. */
        EntityValue body;
        /** @brief The entity of the collider hit. */
        EntityValue collider;
        /** @brief Where the ray met the surface, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief The surface's normal at Point, pointing out of the body. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How far along the ray Point is: 0 for a ray that starts inside a body. */
        float distance = 0.0f;
    };

    /** @brief Where a cast shape hit a body. */
    struct ShapeCastHit
    {
        /** @brief The entity of the body hit. */
        EntityValue body;
        /** @brief The entity of the collider hit. */
        EntityValue collider;
        /** @brief Where the cast shape touches the body, on the cast shape, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief The body's surface normal where it was hit, pointing towards the cast shape. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How far the shape travelled before it touched: 0 for a shape that starts touching. */
        float distance = 0.0f;
    };

    /** @brief Where a moved shape is blocked (PhysicsWorld.MoveCast). */
    struct MoveCastHit
    {
        /** @brief The entity of the body that blocked the shape. */
        EntityValue body;
        /** @brief The entity of the collider that blocked it. */
        EntityValue collider;
        /** @brief Where the shape touches the body, on the shape, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief The body's surface normal where it was touched, pointing towards the shape. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How far the shape can travel along the motion and stay clear of the body: the safe distance. */
        float distance = 0.0f;
    };

    /** @brief A body a shape overlaps, and how to get out of it (PhysicsWorld.Penetrations). */
    struct PenetrationHit
    {
        /** @brief The entity of the body. */
        EntityValue body;
        /** @brief The entity of the collider. */
        EntityValue collider;
        /** @brief The shape's deepest point inside the collider, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief The unit direction that moves the shape out of the collider along the shortest path. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How far along Normal the shape must move to stop overlapping the collider: more than 0. */
        float depth = 0.0f;
    };

    /** @brief A body a shape or point overlaps. */
    struct OverlapHit
    {
        /** @brief The entity of the body. */
        EntityValue body;
        /** @brief The entity of the collider. */
        EntityValue collider;
    };

    /** @brief One contact of a body, as of the last step. */
    struct Contact
    {
        /** @brief The body entity (a rigid body or a character body) asked about. */
        EntityValue body;
        /** @brief The body's collider touching. */
        EntityValue collider;
        /** @brief The body entity (a rigid body or a character body) it touches. */
        EntityValue otherBody;
        /** @brief That body's collider. */
        EntityValue otherCollider;
        /** @brief Where they touch, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief World space, unit length, pointing from Body to OtherBody. */
        Math::Vec3 normal = Math::Vec3(0.0f);
    };
} // namespace Fenrir::ScriptQueries
