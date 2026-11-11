#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "FenrirMath/Frustum.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief What a view's level of detail selection sees of its camera (ADR-0057): the numbers `builtin:cull` chooses
     * each copy's level by, so a game or a test can repeat the choice on the CPU with SelectLod.
     */
    struct LodView
    {
        /** @brief Where the camera is, in world space: while culling is frozen, where it was when frozen. */
        Math::Vec3 position{0.0f};
        /**
         * @brief How many pixels one world unit covers at distance 1: half the viewport's height in pixels over
         * tan(verticalFov / 2) for a perspective camera, the viewport's height over the orthographic height for an
         * orthographic one.
         */
        float pixelsPerUnit = 1.0f;
        /** @brief The near plane's distance: the least distance a perspective camera projects by. */
        float near = 0.1f;
        /** @brief Whether the camera projects by distance; an orthographic one covers the same pixels at any. */
        bool perspective = true;
    };

    /** @brief How many pixels `length` world units cover at `distance` from `view`'s camera. */
    [[nodiscard]] inline float ProjectedPixels(const LodView& view, float length, float distance) noexcept
    {
        return length * view.pixelsPerUnit / (view.perspective ? std::max(distance, view.near) : 1.0f);
    }

    /** @brief The distance from `view`'s position to the nearest point of `sphere`: 0 inside it. */
    [[nodiscard]] inline float DistanceTo(const LodView& view, const Math::Sphere& sphere) noexcept
    {
        return std::max(glm::length(sphere.centre - view.position) - sphere.radius, 0.0f);
    }

    /** @brief The largest scale `transform` applies along any of its axes: what a mesh's lengths are scaled by. */
    [[nodiscard]] inline float LargestScale(const Math::Mat4& transform) noexcept
    {
        return std::max({glm::length(Math::Vec3(transform[0])), glm::length(Math::Vec3(transform[1])),
                         glm::length(Math::Vec3(transform[2]))});
    }

    /**
     * @brief The level of detail the cull pass draws a copy with (ADR-0057), or none when it drops the copy. `errors`
     * are the mesh's levels' errors in its local units, level 0's (0) first and each greater than the last; `box` is
     * the mesh's local bounds and `transform` the copy's world matrix. The copy's distance is from the view's position
     * to the nearest point of the sphere around its box (Math::Frustum::SphereOf), and each error is scaled by the
     * transform's largest axis scale and projected at that distance: the level is the coarsest whose projected error
     * is under `lodErrorPixels`, level 0 when none is. A copy whose sphere's radius projects under
     * `minProjectedPixels` is dropped instead; 0 drops none. The same arithmetic as `builtin:cull`, so the levels the
     * GPU draws are these.
     */
    [[nodiscard]] inline std::optional<std::uint32_t> SelectLod(std::span<const float> errors, const Math::AABB& box,
                                                                const Math::Mat4& transform, const LodView& view,
                                                                float lodErrorPixels, float minProjectedPixels = 0.0f)
    {
        const Math::Sphere sphere = Math::Frustum::SphereOf(box, transform);
        const float distance = DistanceTo(view, sphere);
        if (minProjectedPixels > 0.0f && ProjectedPixels(view, sphere.radius, distance) < minProjectedPixels)
        {
            return std::nullopt;
        }
        const float scale = LargestScale(transform);
        for (std::size_t level = errors.size(); level-- > 1;)
        {
            if (ProjectedPixels(view, errors[level] * scale, distance) < lodErrorPixels)
            {
                return static_cast<std::uint32_t>(level);
            }
        }
        return 0;
    }
} // namespace Fenrir::Render
