#pragma once

#include <cstdint>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief Which bodies a query can find. Optional: by default a query finds every body except triggers.
     */
    struct QueryFilter
    {
        /** @brief The collision layers to find, one bit per layer: a body is found if it is in any of them. */
        std::uint32_t mask = ~0u;
        /** @brief A body never found, such as the one casting the query: the body of this entity (for a child
         * collider's entity, the compound body it belongs to). None by default. */
        Entity ignoreBody = Entity();
        /** @brief Whether triggers are found. */
        bool hitTriggers = false;
    };

    /** @brief Where a ray hit a body. */
    struct RaycastHit
    {
        /** @brief The entity of the body hit. */
        Entity body;
        /** @brief The entity of the collider hit. */
        Entity collider;
        /** @brief Where the ray met the surface, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief The surface's normal at `point`, pointing out of the body. For a ray that starts inside a body, the
         * opposite of the ray's direction. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How far along the ray `point` is: 0 for a ray that starts inside a body. */
        float distance = 0.0f;
    };

    /** @brief Where a cast shape hit a body. */
    struct ShapeCastHit
    {
        /** @brief The entity of the body hit. */
        Entity body;
        /** @brief The entity of the collider hit. */
        Entity collider;
        /** @brief Where the cast shape touches the body, on the cast shape, in world space. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief The body's surface normal where it was hit, pointing towards the cast shape. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How far the shape travelled before it touched: 0 for a shape that starts touching. */
        float distance = 0.0f;
    };

    /** @brief A body a shape or point overlaps. */
    struct OverlapHit
    {
        /** @brief The entity of the body. */
        Entity body;
        /** @brief The entity of the collider. */
        Entity collider;
    };
} // namespace Fenrir
