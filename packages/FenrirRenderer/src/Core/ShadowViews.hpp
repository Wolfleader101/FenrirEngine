#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>

#include "Core/Cascades.hpp"
#include "FenrirMath/Frustum.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // What a shadow view culls against (ADR-0057, culling ticket 10): a cascade's box open towards its light, a spot
    // light's cone, a point light's range sphere. The volumes the shadow views' cull passes test on the GPU, repeated
    // here so a test can be the pass's oracle.

    /**
     * @brief The volume `cascade`'s cull pass tests its casters against: its orthographic box's four side planes and
     * its far plane, in Math::Frustum's order with the near plane left out, so a caster between the light and the
     * cascade's slice, however far towards the light, still casts into it.
     */
    [[nodiscard]] inline Math::Frustum CascadeCullVolume(const Cascade& cascade)
    {
        const Math::Frustum closed = Math::Frustum::FromViewProjection(cascade.viewProjection);
        const std::span<const Math::Vec4> planes = closed.Planes();
        std::array<Math::Vec4, 5> open{};
        std::size_t count = 0;
        for (std::size_t p = 0; p < planes.size() && count < open.size(); ++p)
        {
            // Left, right, bottom, top, then far: the fifth, the near plane, is skipped.
            if (p != 4)
            {
                open.at(count++) = planes[p];
            }
        }
        return Math::Frustum::FromPlanes(std::span<const Math::Vec4>(open).first(count));
    }

    /**
     * @brief Whether the box `box` under `transform` may reach the sphere `range` (a point light's range about its
     * position): false only when the sphere around the box (Math::Frustum::SphereOf) is wholly outside it, or the
     * world box about its eight moved corners is. The test `builtin:cull` repeats for a point light's shadow view,
     * the same arithmetic in the same order.
     */
    [[nodiscard]] inline bool WithinRange(const Math::Sphere& range, const Math::AABB& box, const Math::Mat4& transform)
    {
        const Math::Sphere sphere = Math::Frustum::SphereOf(box, transform);
        const float reach = sphere.radius + range.radius;
        if (glm::dot(sphere.centre - range.centre, sphere.centre - range.centre) > reach * reach)
        {
            return false;
        }
        const std::array<Math::Vec3, 8> corners = Math::Corners(box);
        Math::Vec3 lowest(std::numeric_limits<float>::max());
        Math::Vec3 highest(std::numeric_limits<float>::lowest());
        for (const Math::Vec3& corner : corners)
        {
            const Math::Vec3 moved(transform * Math::Vec4(corner, 1.0f));
            lowest = glm::min(lowest, moved);
            highest = glm::max(highest, moved);
        }
        const Math::Vec3 nearest = glm::clamp(range.centre, lowest, highest);
        const Math::Vec3 away = nearest - range.centre;
        return glm::dot(away, away) <= range.radius * range.radius;
    }
} // namespace Fenrir::Render
